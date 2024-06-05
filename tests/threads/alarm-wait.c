/* N개의 스레드를 생성하고, 각각 다른 고정된 시간 동안 M번 잠듦.
  깨어나는 순서를 기록하고 유효한지 확인합니다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static void test_sleep (int thread_cnt, int iterations);

void
test_alarm_single (void) 
{
  test_sleep (5, 1);
}

void
test_alarm_multiple (void) 
{
  test_sleep (5, 7);
}

/* 테스트에 대한 정보 */
struct sleep_test 
  {
    int64_t start;              /* 테스트 시작 시간 */
    int iterations;             /* 각 스레드당 반복 횟수 */

    /* 출력 */
    struct lock output_lock;    /* 출력 버퍼 보호를 위한 락 */
    int *output_pos;            /* 출력 버퍼의 현재 위치 */
  };

/* 테스트의 개별 스레드에 대한 정보 */
struct sleep_thread 
  {
    struct sleep_test *test;     /* 모든 스레드 간에 공유되는 정보 */
    int id;                     /* 슬리퍼 ID */
    int duration;               /* 슬립할 틱 수 */
    int iterations;             /* 현재까지 카운트된 반복 횟수 */
  };

static void sleeper (void *);

/* THREAD_CNT 개의 스레드를 생성하고, 각각 ITERATIONS 번 잠듦. */
static void
test_sleep (int thread_cnt, int iterations) 
{
  struct sleep_test test;
  struct sleep_thread *threads;
  int *output, *op;
  int product;
  int i;

  /* 이 테스트는 MLFQS와 함께 작동하지 않습니다. */
  ASSERT (!thread_mlfqs);

  msg ("%d개의 스레드를 생성하여 각각 %d번씩 잠들게 합니다.", thread_cnt, iterations);
  msg ("스레드 0은 각각 10 틱씩 잠듭니다,");
  msg ("스레드 1은 각각 20 틱씩 잠듭니다, 그리고 이어갑니다.");
  msg ("성공하면 반복 횟수와 잠자는 시간의 곱이");
  msg ("오름차순으로 나타날 것입니다.");

  /* 메모리 할당 */
  threads = malloc (sizeof *threads * thread_cnt);
  output = malloc (sizeof *output * iterations * thread_cnt * 2);
  if (threads == NULL || output == NULL)
    PANIC ("테스트를 위한 메모리를 할당할 수 없습니다");

  /* 테스트를 초기화합니다. */
  test.start = timer_ticks () + 100;
  test.iterations = iterations;
  lock_init (&test.output_lock);
  test.output_pos = output;

  /* 스레드를 시작합니다. */
  ASSERT (output != NULL);
  for (i = 0; i < thread_cnt; i++)
    {
      struct sleep_thread *t = threads + i;
      char name[16];
      
      t->test = &test;
      t->id = i;
      t->duration = (i + 1) * 10;
      t->iterations = 0;

      snprintf (name, sizeof name, "thread %d", i);
      thread_create (name, PRI_DEFAULT, sleeper, t);
    }
  
  /* 모든 스레드가 완료되기에 충분한 시간을 기다립니다. */
  timer_sleep (100 + thread_cnt * iterations * 10 + 100);

  /* 어떤 돌연변이 스레드가 아직 실행 중인 경우를 대비하여 출력 락을 획득합니다. */
  lock_acquire (&test.output_lock);

  /* 완료 순서를 출력합니다. */
  product = 0;
  for (op = output; op < test.output_pos; op++) 
    {
      struct sleep_thread *t;
      int new_prod;

      ASSERT (*op >= 0 && *op < thread_cnt);
      t = threads + *op;

      new_prod = ++t->iterations * t->duration;
        
      msg ("thread %d: duration=%d, iteration=%d, product=%d",
           t->id, t->duration, t->iterations, new_prod);
      
      if (new_prod >= product)
        product = new_prod;
      else
        fail ("thread %d woke up out of order (%d > %d)!",
              t->id, product, new_prod);
    }

  /* 적절한 깨어남 횟수를 가졌는지 확인합니다. */
  for (i = 0; i < thread_cnt; i++)
    if (threads[i].iterations != iterations)
      fail ("thread %d woke up %d times instead of %d",
            i, threads[i].iterations, iterations);
  
  lock_release (&test.output_lock);
  free (output);
  free (threads);
}

/* 슬리퍼 스레드. */
static void
sleeper (void *t_) 
{
  struct sleep_thread *t = t_;
  struct sleep_test *test = t->test;
  int i;

  for (i = 1; i <= test->iterations; i++) 
    {
      int64_t sleep_until = test->start + i * t->duration;
      timer_sleep (sleep_until - timer_ticks ());
      lock_acquire (&test->output_lock);
      *test->output_pos++ = t->id;
      lock_release (&test->output_lock);
    }
}
