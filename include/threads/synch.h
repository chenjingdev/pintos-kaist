#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* 카운팅 세마포어 */
struct semaphore {
	unsigned value;             /* 현재 값 */
	struct list waiters;        /* 대기 중인 스레드 리스트 */
};

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

/* 락 */
struct lock {
	struct thread *holder;      /* 락을 보유하고 있는 스레드 (디버깅 용도) */
	struct semaphore semaphore; /* 접근을 제어하는 이진 세마포어 */
};

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* 조건 변수 */
struct condition {
	struct list waiters;        /* 대기 중인 스레드 리스트 */
};

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);

/* 최적화 방벽
 *
 * 컴파일러는 최적화 방벽을 가로질러 연산을 재정렬하지 않습니다.
 * 자세한 내용은 참고서의 "Optimization Barriers"를 참조하십시오. */
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
