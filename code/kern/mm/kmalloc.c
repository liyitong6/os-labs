#include <kmalloc.h>
#include <assert.h>

#ifdef USE_KMEM_SLUB
#include <slub_pmm.h>
#include <stdio.h>

/* lab2 2413646: choose the byte algorithm here, independently of PMM. */
static const struct kmem_manager *const kmem_manager = &slub_kmem_manager;
static bool kmem_initialized;
#endif

void
kmem_init(void) {
#ifdef USE_KMEM_SLUB
    assert(!kmem_initialized);
    assert(kmem_manager->init != NULL && kmem_manager->alloc != NULL &&
           kmem_manager->free != NULL);
    kmem_manager->init();
    kmem_initialized = 1;
    cprintf("byte memory management: %s\n", kmem_manager->name);
    if (kmem_manager->check != NULL) {
        kmem_manager->check();
    }
#endif
}

void *
kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
#ifdef USE_KMEM_SLUB
    assert(kmem_initialized);
    return kmem_manager->alloc(size);
#else
    /* Explicit disabled mode, not a fallback allocator. */
    return NULL;
#endif
}

void
kfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }
#ifdef USE_KMEM_SLUB
    assert(kmem_initialized);
    kmem_manager->free(ptr);
#else
    panic("byte allocator disabled: cannot free a non-NULL pointer");
#endif
}
