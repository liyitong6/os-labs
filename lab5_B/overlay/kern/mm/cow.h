#ifndef __KERN_MM_COW_H__
#define __KERN_MM_COW_H__

#include <defs.h>

//lab5 challenge B/C contract 2412933
/* Sv39 RSW bit 8: only an originally writable user mapping may set this bit. */
#ifndef PTE_COW
#define PTE_COW 0x100
#elif PTE_COW != 0x100
#error "B/C must agree on PTE_COW = Sv39 RSW bit 8 (0x100)"
#endif

struct mm_struct;

/* Resolve one COW store fault. Do not advance sepc; the store must retry. */
int cow_resolve_write(struct mm_struct *mm, uintptr_t addr);

/* Before a kernel write to user memory; 0 on success, negative error on failure.
 * On a later-page failure, earlier pages may already be private, but no bytes
 * are written by this helper. Callers must not memcpy unless it returns 0.
 */
int cow_prepare_write(struct mm_struct *mm, uintptr_t addr, size_t len);

#endif
