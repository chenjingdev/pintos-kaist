#ifndef __LIB_KERNEL_LIST_H
#define __LIB_KERNEL_LIST_H

/* 이중 연결 리스트.
 *
 * 이 이중 연결 리스트의 구현은 동적으로 할당된 메모리를 필요로하지 않습니다.
 * 대신, 잠재적인 리스트 요소인 각 구조체는
 * struct list_elem 멤버를 포함해야합니다.
 * 모든 리스트 함수는 이러한 'struct list_elem'에서 작동합니다.
 * list_entry 매크로를 사용하면 struct list_elem에서
 * 포함하는 구조체 객체로의 변환을 수행할 수 있습니다.

 * 예를 들어, 'struct foo'의 리스트가 필요한 경우 'struct foo'는 다음과 같이
 * 'struct list_elem' 멤버를 포함해야합니다.

 * struct foo {
 *   struct list_elem elem;
 *   int bar;
 *   ...other members...
 * };

 * 그런 다음 'struct foo'의 리스트는 다음과 같이 선언 및 초기화 할 수 있습니다.

 * struct list foo_list;

 * list_init (&foo_list);

 * 반복은 struct list_elem에서 포함하는 구조체로
 * 다시 변환해야하는 일반적인 상황입니다.
 * 다음은 foo_list를 사용한 예입니다.

 * struct list_elem *e;

 * for (e = list_begin (&foo_list); e != list_end (&foo_list);
 * e = list_next (e)) {
 *   struct foo *f = list_entry (e, struct foo, elem);
 *   ...f와 함께 작업 수행...
 * }

 * 실제로 리스트 사용 예는 소스 전체에서 찾을 수 있습니다.
 * 예를 들어, threads 디렉토리의
 * malloc.c, palloc.c 및 thread.c가 모두 리스트를 사용합니다.

 * 이 리스트의 인터페이스는 C++ STL의 list<> 템플릿에서 영감을 받았습니다.
 * list<>에 익숙하다면 쉽게 사용할 수 있을 것입니다.
 * 그러나 이러한 리스트는 *타입 검사를 수행하지 않으며* 많은 다른
 * 정확성 검사를 수행 할 수 없습니다. 실수하면 문제가 발생할 수 있습니다.

 * 리스트 용어 설명서:

 * - "front": 리스트의 첫 번째 요소입니다.
 * 비어있는 리스트에서 정의되지 않습니다.
 * list_front()에 의해 반환됩니다.

 * - "back": 리스트의 마지막 요소입니다.
 * 비어있는 리스트에서 정의되지 않습니다.
 * list_back()에 의해 반환됩니다.

 * - "tail": 리스트의 마지막 요소 바로 다음에 있는 요소입니다.
 * 비어있는 리스트에서도 잘 정의됩니다. list_end()에 의해 반환됩니다.
 * 앞에서 뒤로 반복하는 데 사용되는 끝 센티널로 사용됩니다.

 * - "beginning": 비어 있지 않은 리스트에서는 front입니다.
 * 비어있는 리스트에서는 tail입니다. list_begin()에 의해 반환됩니다.
 * 앞에서 뒤로 반복하는 데 시작점으로 사용됩니다.

 * - "head": 첫 번째 요소 바로 앞에 있는 요소입니다.
 * 비어있는 리스트에서도 잘 정의됩니다. list_rend()에 의해 반환됩니다.
 * 뒤에서 앞으로 반복하는 데 끝 센티널로 사용됩니다.

 * - "reverse beginning": 비어 있지 않은 리스트에서는 back입니다.
 * 비어있는 리스트에서는 head입니다. list_rbegin()에 의해 반환됩니다.
 * 뒤에서 앞으로 반복하는 데 시작점으로 사용됩니다.
 *
 * - "interior element": head 또는 tail이 아닌 요소,
 * 즉 실제 리스트 요소입니다.
 * 빈 리스트에는 내부 요소가 없습니다. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 리스트 요소. */
struct list_elem {
   struct list_elem *prev;     /* 이전 리스트 요소. */
   struct list_elem *next;     /* 다음 리스트 요소. */
};

/* 리스트. */
struct list {
   struct list_elem head;      /* 리스트 헤드. */
   struct list_elem tail;      /* 리스트 테일. */
};

/* 포인터를 리스트 요소 LIST_ELEM으로 변환하여
   LIST_ELEM이 포함 된 구조체로의 포인터를 반환합니다.
   외부 구조체 STRUCT의 이름과 리스트 요소의 멤버 이름 MEMBER를 제공하십시오.
   파일 상단의 큰 주석에서 예제를 참조하십시오. */
#define list_entry(LIST_ELEM, STRUCT, MEMBER)           \
	((STRUCT *) ((uint8_t *) &(LIST_ELEM)->next     \
		- offsetof (STRUCT, MEMBER.next)))

void list_init (struct list *);

/* 리스트 순회. */
struct list_elem *list_begin (struct list *);
struct list_elem *list_next (struct list_elem *);
struct list_elem *list_end (struct list *);

struct list_elem *list_rbegin (struct list *);
struct list_elem *list_prev (struct list_elem *);
struct list_elem *list_rend (struct list *);

struct list_elem *list_head (struct list *);
struct list_elem *list_tail (struct list *);

/* 리스트 삽입. */
void list_insert (struct list_elem *, struct list_elem *);
void list_splice (struct list_elem *before,
		struct list_elem *first, struct list_elem *last);
void list_push_front (struct list *, struct list_elem *);
void list_push_back (struct list *, struct list_elem *);

/* 리스트 제거. */
struct list_elem *list_remove (struct list_elem *);
struct list_elem *list_pop_front (struct list *);
struct list_elem *list_pop_back (struct list *);

/* 리스트 요소. */
struct list_elem *list_front (struct list *);
struct list_elem *list_back (struct list *);

/* 리스트 속성. */
size_t list_size (struct list *);
bool list_empty (struct list *);

/* 기타. */
void list_reverse (struct list *);

/* 보조 데이터 AUX가 주어진 두 리스트 요소 A와 B의 값을 비교합니다.
   A가 B보다 작으면 true를 반환하고,
   A가 B보다 크거나 같으면 false를 반환합니다. */
typedef bool list_less_func (const struct list_elem *a,
                             const struct list_elem *b,
                             void *aux);

/* 정렬된 요소를 가진 리스트에 대한 작업. */
void list_sort (struct list *,
                list_less_func *, void *aux);
void list_insert_ordered (struct list *, struct list_elem *,
                          list_less_func *, void *aux);
void list_unique (struct list *, struct list *duplicates,
                  list_less_func *, void *aux);

/* 최대값과 최소값. */
struct list_elem *list_max (struct list *, list_less_func *, void *aux);
struct list_elem *list_min (struct list *, list_less_func *, void *aux);

#endif /* lib/kernel/list.h */
