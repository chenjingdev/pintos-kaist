#include <console.h>
#include <stdarg.h>
#include <stdio.h>
#include "devices/serial.h"
#include "devices/vga.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/synch.h"

static void vprintf_helper (char, void *);
static void putchar_have_lock (uint8_t c);

/* 콘솔 락.
   vga와 serial 계층 모두 자체적으로 락을 사용하므로
   언제든지 호출해도 안전합니다.
   그러나 이 락은 동시에 printf() 호출이 출력이 혼합되는 것을
   방지하는 데 유용합니다. 이는 혼란스러워 보입니다. */
static struct lock console_lock;

/* 일반적인 상황에서 true: 위에서 설명한 것처럼 
   스레드 간 출력이 혼합되지 않도록 콘솔 락을 사용하고자 합니다.

   락이 기능적으로 동작하기 전이나 콘솔 락이 초기화되기 전의 초기 부팅 단계,
   또는 커널 패닉 이후에는 false입니다. 전자의 경우, 락을 취하는 것은
   어설션 실패를 초래하여 패닉을 유발하고, 이는 후자의 경우가 됩니다.
   후자의 경우, buggy한 lock_acquire() 구현이 패닉을 일으켰다면,
   단순히 재귀하게 됩니다. */
static bool use_console_lock;

/* Pintos에 충분한 디버그 출력을 추가하면 단일 스레드에서
   console_lock을 재귀적으로 잡으려고 시도할 수 있습니다.
   실제 예로, palloc_free()에 printf() 호출을 추가한 적이 있습니다.
   다음은 실제로 발생한 백트레이스입니다:

   lock_console()
   vprintf()
   printf()             - palloc()이 다시 락을 잡으려고 시도
   palloc_free()        
   schedule_tail()      - 스레드를 전환하면서 다른 스레드가 종료
   schedule()
   thread_yield()
   intr_handler()       - 타이머 인터럽트
   intr_set_level()
   serial_putc()
   putchar_have_lock()
   putbuf()
   sys_write()          - 콘솔에 쓰는 한 프로세스
   syscall_handler()
   intr_handler()

   이러한 상황은 디버깅하기 매우 어렵기 때문에 깊이 카운터를 사용하여
   재귀적 락을 시뮬레이션하여 문제를 피합니다. */
static int console_lock_depth;

/* 콘솔에 기록된 문자 수. */
static int64_t write_cnt;

/* 콘솔 락 활성화. */
void
console_init (void) {
	lock_init (&console_lock);
	use_console_lock = true;
}

/* 커널 패닉이 진행 중임을 콘솔에 알리며,
   앞으로 콘솔 락을 취하지 않도록 경고합니다. */
void
console_panic (void) {
	use_console_lock = false;
}

/* 콘솔 통계를 출력합니다. */
void
console_print_stats (void) {
	printf ("Console: %lld characters output\n", write_cnt);
}

/* 콘솔 락을 획득합니다. */
static void
acquire_console (void) {
	if (!intr_context () && use_console_lock) {
		if (lock_held_by_current_thread (&console_lock)) 
			console_lock_depth++; 
		else
			lock_acquire (&console_lock); 
	}
}

/* 콘솔 락을 해제합니다. */
static void
release_console (void) {
	if (!intr_context () && use_console_lock) {
		if (console_lock_depth > 0)
			console_lock_depth--;
		else
			lock_release (&console_lock); 
	}
}

/* 현재 스레드가 콘솔 락을 가지고 있으면 true를,
   그렇지 않으면 false를 반환합니다. */
static bool
console_locked_by_current_thread (void) {
	return (intr_context ()
			|| !use_console_lock
			|| lock_held_by_current_thread (&console_lock));
}

/* 표준 vprintf() 함수,
   printf()와 비슷하지만 va_list를 사용합니다.
   출력을 vga 디스플레이와 시리얼 포트에 기록합니다. */
int
vprintf (const char *format, va_list args) {
	int char_cnt = 0;

	acquire_console ();
	__vprintf (format, args, vprintf_helper, &char_cnt);
	release_console ();

	return char_cnt;
}

/* 문자열 S를 콘솔에 출력하고, 새 줄 문자로 끝맺습니다. */
int
puts (const char *s) {
	acquire_console ();
	while (*s != '\0')
		putchar_have_lock (*s++);
	putchar_have_lock ('\n');
	release_console ();

    return 0;
}

/* BUFFER에 있는 N 문자를 콘솔에 출력합니다. */
void
putbuf (const char *buffer, size_t n) {
	acquire_console ();
	while (n-- > 0)
		putchar_have_lock (*buffer++);
	release_console ();
}

/* C를 vga 디스플레이와 시리얼 포트에 기록합니다. */
int
putchar (int c) {
	acquire_console ();
	putchar_have_lock (c);
	release_console ();

    return c;
}

/* vprintf()의 헬퍼 함수. */
static void
vprintf_helper (char c, void *char_cnt_) {
	int *char_cnt = char_cnt_;
	(*char_cnt)++;
	putchar_have_lock (c);
}

/* C를 vga 디스플레이와 시리얼 포트에 기록합니다.
   호출자는 이미 콘솔 락을 획득했습니다. */
static void
putchar_have_lock (uint8_t c) {
	ASSERT (console_locked_by_current_thread ());
	write_cnt++;
	serial_putc (c);
	vga_putc (c);
}
