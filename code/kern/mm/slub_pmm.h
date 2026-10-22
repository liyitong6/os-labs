#ifndef __KERN_MM_SLUB_PMM_H__
#define __KERN_MM_SLUB_PMM_H__

#include <kmalloc.h>

/* lab2 2413646: selection layer only; clients include kmalloc.h instead. */
extern const struct kmem_manager slub_kmem_manager;

#endif /* !__KERN_MM_SLUB_PMM_H__ */
