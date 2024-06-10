#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* struct thread의 `magic' 멤버에 대한 랜덤 값.
   스택 오버플로우를 감지하는 데 사용됩니다.
   세부 사항은 thread.h 맨 위에 있는 큰 주석을 참조하십시오. */
#define THREAD_MAGIC 0xcd6abf4b

/* 기본 스레드에 대한 랜덤 값
   이 값을 수정하지 마십시오. */
#define THREAD_BASIC 0xd42df210

/* THREAD_READY 상태에 있는 프로세스 목록,
   즉 실행 준비가 되었지만 실제로 실행 중이지 않은 프로세스 목록. */
static struct list ready_list;

static struct list sleep_list;

int64_t next_tick_to_awake = INT64_MAX;

/* 유휴 스레드. */
static struct thread *idle_thread;

/* 초기 스레드, init.c:main()을 실행하는 스레드. */
static struct thread *initial_thread;

/* allocate_tid()에서 사용하는 잠금. */
static struct lock tid_lock;

/* 스레드 파괴 요청 */
static struct list destruction_req;

/* 통계. */
static long long idle_ticks;    /* 유휴 상태에서 소요된 타이머 틱 수. */
static long long kernel_ticks;  /* 커널 스레드에서의 타이머 틱 수. */
static long long user_ticks;    /* 사용자 프로그램에서의 타이머 틱 수. */

/* 스케줄링. */
#define TIME_SLICE 4            /* 각 스레드에 할당할 타이머 틱 수. */
static unsigned thread_ticks;   /* 마지막 양보 이후의 타이머 틱 수. */

/* false(기본값)이면 라운드 로빈 스케줄러를 사용합니다.
   true이면 다단계 피드백 큐 스케줄러를 사용합니다.
   커널 명령 줄 옵션 "-o mlfqs"로 제어됩니다. */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* T가 유효한 스레드를 가리키는 것처럼 보이면 true를 반환합니다. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* 실행 중인 스레드를 반환합니다.
 * CPU의 스택 포인터 `rsp'를 읽고, 그 값을 페이지의 시작 부분으로 반올림합니다.
 * `struct thread'는 항상 페이지의 시작 부분에 있고,
 * 스택 포인터는 중간 어딘가에 있기 때문에
 * 현재 스레드를 찾을 수 있습니다. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// thread_start를 위한 글로벌 디스크립터 테이블.
// gdt가 thread_init 후에 설정되기 때문에,
// 임시 gdt를 먼저 설정해야 합니다.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* 현재 실행 중인 코드를 스레드로 변환하여 스레딩 시스템을 초기화합니다.
   일반적으로는 작동하지 않으며, 이 경우에는 loader.S가
   스택의 하단을 페이지 경계에 놓도록 신경 썼기 때문에 가능합니다.

   또한 실행 대기열과 tid 잠금을 초기화합니다.

   이 함수 호출 후, thread_create()를 사용하여
   스레드를 생성하기 전에 페이지 할당자를 초기화해야 합니다.

   이 함수가 완료될 때까지 thread_current()를 호출하는 것은 안전하지 않습니다. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* 커널을 위한 임시 gdt를 다시 로드합니다.
	 * 이 gdt는 사용자 컨텍스트를 포함하지 않습니다.
	 * 커널은 gdt_init()에서 사용자 컨텍스트와 함께 gdt를 재구축합니다. */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* 글로벌 스레드 컨텍스트 초기화 */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&sleep_list);
	list_init (&destruction_req);

	/* 실행 중인 스레드를 위한 스레드 구조체 설정 */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* 인터럽트를 활성화하여 선점형 스레드 스케줄링을 시작합니다.
   또한 유휴 스레드를 생성합니다. */
void
thread_start (void) {
	/* 유휴 스레드를 생성합니다. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* 선점형 스레드 스케줄링 시작 */
	intr_enable ();

	/* idle_thread를 초기화하기 위해 유휴 스레드가 초기화될 때까지 대기합니다. */
	sema_down (&idle_started);
}

/* 타이머 인터럽트 핸들러에 의해 각 타이머 틱마다 호출됩니다.
   따라서 이 함수는 외부 인터럽트 컨텍스트에서 실행됩니다. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* 통계 업데이트 */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* 선점 강제 */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* 스레드 통계를 출력합니다. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* 주어진 초기 PRIORITY로 이름이 NAME인 새 커널 스레드를 생성하고,
   AUX를 인수로 전달하여 FUNCTION을 실행하고, 준비 큐에 추가합니다.
   새 스레드의 스레드 식별자를 반환하거나, 생성에 실패하면 TID_ERROR를 반환합니다.

   thread_start()가 호출되었다면,
   새 스레드는 thread_create()가 반환되기 전에 스케줄될 수 있습니다.
   심지어 thread_create()가 반환되기 전에 종료될 수도 있습니다.
   반대로, 원래 스레드는 새 스레드가 스케줄되기 전에 얼마든지 실행될 수 있습니다.
   순서를 보장해야 하는 경우 세마포어나 다른 동기화 수단을 사용하십시오.

   제공된 코드는 새 스레드의 `priority' 멤버를 PRIORITY로 설정하지만
   실제로 우선 순위 스케줄링은 구현되지 않았습니다.
   우선 순위 스케줄링은 문제 1-3의 목표입니다. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* 스레드 할당 */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* 스레드 초기화 */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* 스케줄링되면 kernel_thread를 호출합니다.
	 * 참고) rdi는 첫 번째 인수, rsi는 두 번째 인수입니다. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* 실행 큐에 추가합니다. */
	thread_unblock (t);

	/* 생성된 스레드의 우선순위가 현재 실행중인
	   스레드의 우선순위 보다 높다면 현재 실행중인 스레드는 CPU를 양보한다.
	   이때 thread_unblock에서 우선순위로 ready_list를 정렬한 상태이기 때문에
	   다음 실행될 스레드는 우선 순위가 높은 새로 생성된 스레드가 실행되게 된다.*/
	if (thread_current()->priority < priority) {
		thread_yield();
	}

	return tid;
}

/* 현재 스레드를 슬립 상태로 만듭니다.
   thread_unblock()에 의해 깨어날 때까지 다시 스케줄되지 않습니다.

   이 함수는 인터럽트가 꺼진 상태에서 호출해야 합니다.
   보통 synch.h의 동기화 원시 중 하나를 사용하는 것이 더 나은 방법입니다. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* 블록된 스레드 T를 실행 준비 상태로 전환합니다.
   T가 블록되지 않은 경우 오류입니다.
   (실행 중인 스레드를 준비 상태로 만들려면 thread_yield()를 사용하십시오.)

   이 함수는 실행 중인 스레드를 선점하지 않습니다.
   이것은 중요할 수 있습니다: 호출자가 인터럽트를 비활성화한 경우,
   스레드를 원자적으로 차단 해제하고
   다른 데이터를 업데이트할 것으로 예상할 수 있습니다. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	
	// 우선순위로 ready_list에 삽입
	list_insert_ordered(&ready_list, &t->elem, cmp_priority, NULL);

	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* 실행 중인 스레드의 이름을 반환합니다. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* 실행 중인 스레드를 반환합니다.
   이는 running_thread()와 몇 가지 안정성 검사를 추가한 것입니다.
   세부 사항은 thread.h의 맨 위에 있는 큰 주석을 참조하십시오. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* T가 실제로 스레드인지 확인하십시오.
	   이 어설션 중 하나가 발동되면 스레드가 스택을 오버플로우했을 수 있습니다.
	   각 스레드는 4kB 미만의 스택을 가지고 있으므로
	   몇 개의 큰 자동 배열 또는 중간 정도의 재귀가
	   스택 오버플로우를 일으킬 수 있습니다. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* 실행 중인 스레드의 tid를 반환합니다. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* 현재 스레드를 디스케줄링하고 파괴합니다.
   호출자에게 절대 반환되지 않습니다. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* 상태를 죽음으로 설정하고 다른 프로세스를 스케줄링합니다.
	   schedule_tail() 호출 중에 파괴될 것입니다. */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* CPU를 양보합니다. 현재 스레드는 잠자지 않고
   스케줄러의 변덕에 따라 즉시 다시 스케줄될 수 있습니다. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		list_insert_ordered(&ready_list, &curr->elem, cmp_priority, NULL);
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* 현재 스레드의 우선 순위를 NEW_PRIORITY로 설정합니다. */
void
thread_set_priority (int new_priority) {
	thread_current ()->priority = new_priority;
	/* 스레드의 우선순위가 변경되었을 때
	우선순위에 따라 선점이 발생하도록 한다. */
	test_max_priority();
}

void
test_max_priority (void) {
	/* read_list에서 우선순위가 가장 높은 스레드와 현재 스레드의
	우선순위를 비교하여 스케줄링 한다.(ready_list 가 비여있지 않은지 확인)  */
	if (!list_empty(&ready_list) && list_entry(list_front(&ready_list), struct thread, elem)->priority > thread_current()->priority) {
		thread_yield();
	}
}

bool
cmp_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
	return list_entry(a, struct thread, elem)->priority > list_entry(b, struct thread, elem)->priority;
}

/* 현재 스레드의 우선 순위를 반환합니다. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* 현재 스레드의 nice 값을 NICE로 설정합니다. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: 구현을 여기서 작성하십시오 */
}

/* 현재 스레드의 nice 값을 반환합니다. */
int
thread_get_nice (void) {
	/* TODO: 구현을 여기서 작성하십시오 */
	return 0;
}

/* 시스템 로드 평균의 100배를 반환합니다. */
int
thread_get_load_avg (void) {
	/* TODO: 구현을 여기서 작성하십시오 */
	return 0;
}

/* 현재 스레드의 recent_cpu 값의 100배를 반환합니다. */
int
thread_get_recent_cpu (void) {
	/* TODO: 구현을 여기서 작성하십시오 */
	return 0;
}

/* 유휴 스레드. 다른 실행 준비된 스레드가 없을 때 실행됩니다.

   유휴 스레드는 thread_start()에 의해 처음 준비 목록에 추가됩니다.
   처음에는 한 번 스케줄된 후, idle_thread를 초기화하고,
   thread_start()가 계속할 수 있도록 전달된 세마포어를 "up"하고, 즉시 차단됩니다.
   그 후, 유휴 스레드는 준비 목록에 나타나지 않습니다.
   준비 목록이 비어 있을 때 next_thread_to_run()에 의해 특수한 경우로 반환됩니다. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* 다른 사람이 실행하게 합니다. */
		intr_disable ();
		thread_block ();

		/* 인터럽트를 다시 활성화하고 다음 인터럽트를 기다립니다.

		   `sti' 명령은 다음 명령이 완료될 때까지 인터럽트를 비활성화합니다.
		   따라서 이 두 명령은 원자적으로 실행됩니다.
		   이 원자성이 중요합니다; 그렇지 않으면 인터럽트를 다시 활성화하고
		   다음 인터럽트가 발생할 때까지 기다리는 사이에 인터럽트가
		   처리될 수 있어 최대 한 클럭 틱 만큼의 시간이 낭비될 수 있습니다.

		   [IA32-v2a] "HLT", [IA32-v2b] "STI", 및 [IA32-v3a] 7.11.1 "HLT Instruction"을 참조하십시오. */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* 커널 스레드의 기본으로 사용되는 함수. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* 스케줄러는 인터럽트가 꺼진 상태에서 실행됩니다. */
	function (aux);       /* 스레드 함수를 실행합니다. */
	thread_exit ();       /* function()이 반환되면 스레드를 종료합니다. */
}


/* T를 NAME으로 블록된 스레드로 기본 초기화합니다. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
}

/* 스케줄링될 다음 스레드를 선택하고 반환합니다.
   실행 대기열에 있는 스레드를 반환해야 합니다.
   실행 대기열이 비어 있으면 유휴 스레드를 반환합니다. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* iretq를 사용하여 스레드를 시작합니다. */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* 새 스레드의 페이지 테이블을 활성화하고, 이전 스레드가 죽어가는 경우 파괴합니다.

   이 함수의 호출 시점에서 우리는 막 thread PREV에서 전환했으며, 새 스레드는 이미 실행 중이고,
   인터럽트는 여전히 비활성화되어 있습니다.

   thread switch가 완료될 때까지 printf()를 호출하는 것은 안전하지 않습니다.
   실제로 이는 함수 끝에 printf()를 추가해야 한다는 것을 의미합니다. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* 주 스위칭 로직.
	 * 먼저 전체 실행 컨텍스트를 intr_frame에 복원한 다음
	 * do_iret을 호출하여 다음 스레드로 전환합니다.
	 * 여기서부터 전환이 완료될 때까지
	 * 어떤 스택도 사용해서는 안 됩니다. */
	__asm __volatile (
			/* 사용할 레지스터 저장 */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* 한 번 입력 가져오기 */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // 저장된 rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // 저장된 rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // 저장된 rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // 현재 rip 읽기
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* 새 프로세스를 스케줄링합니다. 진입 시 인터럽트는 꺼져 있어야 합니다.
 * 이 함수는 현재 스레드의 상태를 status로 수정한 다음,
 * 실행할 다른 스레드를 찾아 스위칭합니다.
 * schedule()에서는 printf()를 호출하는 것이 안전하지 않습니다. */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* 실행 중으로 표시합니다. */
	next->status = THREAD_RUNNING;

	/* 새 타임 슬라이스 시작 */
	thread_ticks = 0;

#ifdef USERPROG
	/* 새로운 주소 공간 활성화 */
	process_activate (next);
#endif

	if (curr != next) {
		/* 전환한 스레드가 죽어가는 경우, 그 struct thread를 파괴합니다.
		   이는 thread_exit()가 스스로를 파괴하지 않도록 늦게 일어나야 합니다.
		   현재 스택이 사용 중이기 때문에
		   여기서는 페이지 해제 요청만 대기열에 추가합니다.
		   실제 파괴 로직은 schedule()의 시작 부분에서 호출됩니다. */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* 스레드를 스위칭하기 전에, 현재 실행 정보를 저장합니다. */
		thread_launch (next);
	}
}

/* 새 스레드에 사용할 tid를 반환합니다. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

void thread_sleep(int64_t ticks) {
	enum intr_level old_level = intr_disable();;
	struct thread *t = thread_current();

	if (t != idle_thread) {
		t->wake_tick = ticks;
		list_push_back(&sleep_list, &t->elem);
		thread_block();
	}

	intr_set_level(old_level);
};

void thread_awake(int64_t ticks) {
    struct list_elem *e = list_begin(&sleep_list);

    while (e != list_end(&sleep_list)) {
        struct thread *t = list_entry(e, struct thread, elem);
        struct list_elem *next = list_next(e);  // 다음 요소 미리 저장

        if (t->wake_tick <= ticks) {
            list_remove(e);  // 요소를 리스트에서 제거
            thread_unblock(t);  // 스레드 언블록
        }

        e = next;  // 다음 요소로 이동
    }
}