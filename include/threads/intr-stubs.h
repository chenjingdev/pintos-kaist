#ifndef THREADS_INTR_STUBS_H
#define THREADS_INTR_STUBS_H

/* 인터럽트 스텁.
 *
 * 이는 intr-stubs.S에 있는 작은 코드 조각들로, 
 * 256개의 가능한 x86 인터럽트 각각에 하나씩 있습니다. 
 * 각 스텁은 약간의 스택 조작을 수행한 후 intr_entry()로 점프합니다. 
 * 자세한 내용은 intr-stubs.S를 참조하십시오.
 *
 * 이 배열은 intr_init()이 쉽게 찾을 수 있도록 
 * 각 인터럽트 스텁 진입 지점을 가리킵니다. */
typedef void intr_stub_func (void);
extern intr_stub_func *intr_stubs[256];

#endif /* threads/intr-stubs.h */
