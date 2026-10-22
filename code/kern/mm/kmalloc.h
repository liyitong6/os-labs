#ifndef __KERN_MM_KMALLOC_H__
#define __KERN_MM_KMALLOC_H__

#include <defs.h>

/* lab2 2413646: algorithm-independent, byte-oriented allocation contract.
 * Calls are serialized. A successful allocation provides at least size bytes
 * aligned to 16 bytes; it is not implicitly zeroed. Free the original pointer.
 */
struct kmem_manager {
    const char *name;
    void (*init)(void);
    void *(*alloc)(size_t size);
    void (*free)(void *ptr);
    void (*check)(void);  // lab2 2413646: optional byte-layer self-check
};

/* Initialize once, after pmm_init. KMEM=0 makes initialization a no-op. */
void kmem_init(void);
/* Zero size (or KMEM=0) returns NULL; positive enabled calls require init. */
void *kmalloc(size_t size);
/* NULL is harmless; all other pointers must be currently owned allocations. */
void kfree(void *ptr);

#endif /* !__KERN_MM_KMALLOC_H__ */
