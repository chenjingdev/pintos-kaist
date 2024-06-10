#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* 스레드의 생애 주기 상태 */
enum thread_status {
	THREAD_RUNNING,     /* 실행 중인 스레드 */
	THREAD_READY,       /* 실행 준비가 된 스레드 */
	THREAD_BLOCKED,     /* 이벤트를 기다리는 중인 스레드 */
	THREAD_DYING        /* 곧 파괴될 스레드 */
};

/* 스레드 식별자 타입.
   원하는 타입으로 재정의할 수 있습니다. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* tid_t에 대한 오류 값 */

/* 스레드 우선순위 */
#define PRI_MIN 0                       /* 최저 우선순위 */
#define PRI_DEFAULT 31                  /* 기본 우선순위 */
#define PRI_MAX 63                      /* 최고 우선순위 */

/* 커널 스레드 또는 사용자 프로세스
 *
 * 각 스레드 구조체는 자신의 4 kB 페이지에 저장됩니다.
 * 스레드 구조체 자체는 페이지 맨 아래(오프셋 0)에 위치합니다.
 * 페이지의 나머지는 스레드의 커널 스택을 위해 예약되어 있으며,
 * 커널 스택은 페이지 맨 위(오프셋 4 kB)에서 아래로 성장합니다.
 * 다음은 그 예시입니다:
 *
 *      4 kB +---------------------------------+
 *           |          커널 스택               |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         아래로 성장              |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * 이로 인해 두 가지 결론이 나옵니다:
 *
 *    1. 첫째, `struct thread'는 너무 커지지 않도록 해야 합니다.
 *       그렇지 않으면 커널 스택을 위한 공간이 부족해집니다.
 *       기본 `struct thread'는 몇 바이트 크기입니다.
 *       1 kB 이하로 유지하는 것이 좋습니다.
 *
 *    2. 둘째, 커널 스택은 너무 커지지 않도록 해야 합니다.
 *       스택이 오버플로우되면 스레드 상태가 손상됩니다.
 *       따라서 커널 함수는 큰 구조체나 배열을 
 * 		 비정적 지역 변수로 할당하지 않아야 합니다.
 *       대신 malloc() 또는 palloc_get_page()를 사용하여
 * 		 동적 할당을 사용하십시오.
 *
 * 이 문제 중 어느 하나의 첫 번째 증상은
 * 실행 중인 스레드의 `struct thread'의 `magic' 멤버가
 * THREAD_MAGIC으로 설정되어 있는지 확인하는
 * thread_current()에서 어설션 실패가 발생할 가능성이 높습니다.
 * 스택 오버플로우는 일반적으로 이 값을 변경하여 어설션을 트리거합니다. */
/* `elem' 멤버는 이중 목적을 가집니다.
 * 실행 큐의 요소(thread.c)일 수도 있고,
 * 세마포어 대기 리스트의 요소(synch.c)일 수도 있습니다.
 * 이는 두 상태가 상호 배타적이기 때문에 두 가지 방식으로만 사용될 수 있습니다:
 * 준비 상태의 스레드만 실행 큐에 있고,
 * 블록 상태의 스레드만 세마포어 대기 리스트에 있습니다. */
struct thread {
	/* thread.c에서 소유합니다. */
	tid_t tid;                          /* 스레드 식별자 */
	enum thread_status status;          /* 스레드 상태 */
	char name[16];                      /* 이름 (디버깅 용도) */
	uint8_t *stack; 				    /* 저장된 스택 포인터 */
	int priority;                       /* 우선순위 */
	struct list_elem allelem;           /* 모든 스레드 목록을 위한 리스트 요소 */

	/* thread.c와 synch.c 사이에서 공유합니다. */
	struct list_elem elem;              /* 리스트 요소 */
	int64_t wake_tick;					/* 깨어날 시간 */


#ifdef USERPROG
	/* userprog/process.c에서 소유합니다. */
	uint64_t *pml4;                     /* 페이지 맵 레벨 4 */
#endif
#ifdef VM
	/* 스레드가 소유한 전체 가상 메모리 테이블 */
	struct supplemental_page_table spt;
#endif

	/* thread.c에서 소유합니다. */
	struct intr_frame tf;               /* 스위칭 정보 */
	unsigned magic;                     /* 스택 오버플로우 감지 */
};

/* false(기본값)이면 라운드 로빈 스케줄러를 사용합니다.
   true이면 다단계 피드백 큐 스케줄러를 사용합니다.
   커널 명령 줄 옵션 "-o mlfqs"로 제어됩니다. */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

void thread_sleep (int64_t ticks);
void thread_awake (int64_t ticks);
void print_thread (struct thread *t);

#endif /* threads/thread.h */
