#ifndef LAB5_B_FIXTURE_PMM_H
#define LAB5_B_FIXTURE_PMM_H
#include <defs.h>
#include <mmu.h>
#include <memlayout.h>
/* Fixture platform services, not C's PMM implementation. */
struct Page *alloc_pages(size_t n);
void free_pages(struct Page *p,size_t n);
#define alloc_page() alloc_pages(1)
#define free_page(p) free_pages((p),1)
pte_t *get_pte(pde_t *root,uintptr_t va,bool create);
int page_insert(pde_t *root,struct Page *p,uintptr_t va,uint32_t perm);
struct Page *pte2page(pte_t pte);
void *page2kva(struct Page *p);
uintptr_t page2pa(struct Page *p);
static inline int page_ref(struct Page *p) { return p->ref; }
static inline void set_page_ref(struct Page *p,int n) { p->ref=n; }
static inline void flush_tlb(void) { asm volatile("sfence.vma" ::: "memory"); }
void tlb_invalidate(pde_t *root,uintptr_t va);
void print_pgdir(void);
#endif
