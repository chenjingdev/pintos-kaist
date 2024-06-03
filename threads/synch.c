/* 이 파일은 Nachos 교육용 운영체제의 소스 코드에서 파생되었습니다.
   Nachos 저작권 고지는 아래에 전체적으로 재현됩니다. */

/* 저작권 (c) 1992-1996 캘리포니아 대학교 리전츠.
   모든 권리 보유.

   본 소프트웨어 및 그 문서를 사용, 복사, 수정, 배포하는 권한이
   어떠한 목적으로든, 비용 없이, 서면 합의 없이 부여되며,
   위 저작권 고지 및 다음 두 단락이 이 소프트웨어의 모든 복사본에
   포함되어야 합니다.

   캘리포니아 대학교는 본 소프트웨어 및 그 문서의 사용으로 인해 발생하는
   직접적, 간접적, 특별한, 부수적, 또는 결과적 손해에 대해 책임지지 않습니다.
   캘리포니아 대학교가 그러한 손해의 가능성을 알고 있더라도 마찬가지입니다.

   캘리포니아 대학교는 특히 상품성 및 특정 목적에 대한 적합성에 대한 묵시적 보증을
   포함하되 이에 국한되지 않는 모든 보증을 명시적으로 부인합니다.
   본 소프트웨어는 "있는 그대로" 제공되며, 캘리포니아 대학교는
   유지보수, 지원, 업데이트, 개선 또는 수정 의무가 없습니다.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* 세마포어 SEMA를 VALUE로 초기화합니다. 세마포어는
   비음수 정수와 이를 조작하기 위한 두 가지 원자 연산자로 구성됩니다:

   - down 또는 "P": 값이 양수가 될 때까지 기다렸다가
   감소시킵니다.

   - up 또는 "V": 값을 증가시키고 (있다면) 기다리는 하나의 스레드를 깨웁니다. */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* 세마포어에 대한 down 또는 "P" 연산. SEMA의 값이
   양수가 될 때까지 기다린 후 원자적으로 값을 감소시킵니다.

   이 함수는 슬립할 수 있으므로 인터럽트 핸들러 내에서 호출하면 안 됩니다.
   이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만,
   슬립하면 다음 스케줄된 스레드는 아마도 인터럽트를 다시 활성화할 것입니다.
   이 함수는 sema_down 함수입니다. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		list_push_back (&sema->waiters, &thread_current ()->elem);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* 세마포어에 대한 down 또는 "P" 연산을 시도하지만,
   세마포어가 이미 0인 경우에는 시도하지 않습니다.
   세마포어가 감소된 경우 true를 반환하고, 그렇지 않으면 false를 반환합니다.

   이 함수는 인터럽트 핸들러에서 호출될 수 있습니다. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* 세마포어에 대한 up 또는 "V" 연산. SEMA의 값을 증가시키고
   SEMA를 기다리는 스레드 중 하나를 깨웁니다 (있다면).

   이 함수는 인터럽트 핸들러에서 호출될 수 있습니다. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters))
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	sema->value++;
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* 세마포어에 대한 자기 테스트로, 두 스레드 사이에 제어를 "핑퐁"하게 만듭니다.
   printf() 호출을 삽입하여 어떤 일이 일어나고 있는지 확인하십시오. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* sema_self_test()에서 사용하는 스레드 함수. */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* LOCK을 초기화합니다. 잠금은 특정 시간에 최대 한 개의
   스레드만 보유할 수 있습니다. 우리의 잠금은 "재귀적"이지 않습니다.
   즉, 현재 잠금을 보유한 스레드가 해당 잠금을 다시 획득하려고 하면 오류입니다.

   잠금은 초기 값이 1인 세마포어의 특수화입니다.
   잠금과 세마포어의 차이점은 두 가지입니다.
   첫째, 세마포어는 값이 1보다 클 수 있지만,
   잠금은 한 번에 하나의 스레드만 소유할 수 있습니다.
   둘째, 세마포어에는 소유자가 없으므로, 한 스레드가
   세마포어를 "다운"하고 다른 스레드가 "업"할 수 있지만,
   잠금에서는 동일한 스레드가 잠금을 획득하고 해제해야 합니다.
   이러한 제한이 부담스러울 때는 잠금 대신
   세마포어를 사용하는 것이 좋습니다. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* LOCK을 획득합니다. 필요하다면 잠금이 사용 가능해질 때까지 슬립합니다.
   현재 스레드는 이미 잠금을 보유하고 있지 않아야 합니다.

   이 함수는 슬립할 수 있으므로 인터럽트 핸들러 내에서 호출하면 안 됩니다.
   이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만,
   슬립이 필요하면 인터럽트가 다시 활성화될 것입니다. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	sema_down (&lock->semaphore);
	lock->holder = thread_current ();
}

/* LOCK을 획득하려 시도하고 성공하면 true를 반환하고,
   실패하면 false를 반환합니다.
   현재 스레드는 이미 잠금을 보유하고 있지 않아야 합니다.

   이 함수는 슬립하지 않으므로 인터럽트 핸들러 내에서 호출될 수 있습니다. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* 현재 스레드가 소유한 LOCK을 해제합니다.
   이 함수는 lock_release 함수입니다.

   인터럽트 핸들러는 잠금을 획득할 수 없으므로,
   인터럽트 핸들러 내에서 잠금을 해제하려고
   하는 것은 의미가 없습니다. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* 현재 스레드가 LOCK을 보유하고 있으면 true를 반환하고,
   그렇지 않으면 false를 반환합니다.
   (다른 스레드가 잠금을 보유하고 있는지 테스트하는 것은
   경쟁 상태를 초래할 수 있습니다.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* 리스트의 세마포어 중 하나. */
struct semaphore_elem {
	struct list_elem elem;              /* 리스트 요소. */
	struct semaphore semaphore;         /* 이 세마포어. */
};

/* 조건 변수 COND를 초기화합니다. 조건 변수는
   한 코드가 조건을 신호하고 협력하는 코드가
   신호를 받고 이에 따라 행동할 수 있게 합니다. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* LOCK을 원자적으로 해제하고 COND가 다른 코드에 의해 신호될 때까지 기다립니다.
   COND가 신호된 후 LOCK을 다시 획득한 후 반환됩니다.
   이 함수를 호출하기 전에 LOCK이 보유되어야 합니다.

   이 함수가 구현하는 모니터는 "메사" 스타일이며, "호어" 스타일이 아닙니다.
   즉, 신호를 보내고 받는 것은 원자적 연산이 아닙니다.
   따라서 보통 호출자는 대기 완료 후 조건을 다시 확인하고,
   필요한 경우 다시 기다려야 합니다.

   주어진 조건 변수는 하나의 잠금에만 연관될 수 있지만,
   하나의 잠금은 여러 조건 변수와 연관될 수 있습니다.
   즉, 잠금에서 조건 변수로의 매핑은 일대다 관계입니다.

   이 함수는 슬립할 수 있으므로 인터럽트 핸들러 내에서 호출하면 안 됩니다.
   이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만,
   슬립이 필요하면 인터럽트가 다시 활성화될 것입니다. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_push_back (&cond->waiters, &waiter.elem);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* LOCK으로 보호된 COND에서 기다리는 스레드가 있다면,
   이 함수는 그 중 하나에게 신호를 보내 기다림을 종료하게 합니다.
   이 함수를 호출하기 전에 LOCK이 보유되어야 합니다.

   인터럽트 핸들러는 잠금을 획득할 수 없으므로,
   인터럽트 핸들러 내에서 조건 변수에 신호를 보내려고 하는 것은 의미가 없습니다. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters))
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
}

/* LOCK으로 보호된 COND에서 기다리는 모든 스레드를 깨웁니다 (있다면).
   이 함수를 호출하기 전에 LOCK이 보유되어야 합니다.

   인터럽트 핸들러는 잠금을 획득할 수 없으므로,
   인터럽트 핸들러 내에서 조건 변수에 신호를 보내려고 하는 것은 의미가 없습니다. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
