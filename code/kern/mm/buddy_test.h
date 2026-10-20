#ifndef __KERN_MM_BUDDY_TEST_H__
#define __KERN_MM_BUDDY_TEST_H__

#include <pmm.h>

/* lab2 2413646: enabled only for explicitly selected Buddy test builds. */
void buddy_check(void);
void buddy_test_init_memmap(struct Page *base, size_t n);

#endif
