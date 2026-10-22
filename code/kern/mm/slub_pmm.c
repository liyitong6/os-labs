#include <slub_pmm.h>
#ifdef SLUB_TEST
#include <slub_test.h>
#endif
#include <pmm.h>
#include <list.h>
#include <string.h>

/* lab2 2413646: private object metadata never modifies struct Page.
 * Eight static caches own lists of single-page slabs. Each slab carries
 * its own allocation bitmap (one bit per slot) for invalid-free checks.
 */
#define SLUB_ALIGNMENT 16
#define SLUB_CACHE_COUNT 8
#define SLUB_MAX_SMALL 2048
#define SLUB_BITMAP_WORDS ((PGSIZE / SLUB_ALIGNMENT + 63) / 64)

struct slub_cache {
    size_t object_size;
    list_entry_t slabs;
};

struct slub_slab {
    struct slub_cache *cache;
    struct Page *page;
    list_entry_t link;
    size_t inuse;
    void *freelist;
    uint64_t allocated[SLUB_BITMAP_WORDS];
};

struct slub_large {
    struct Page *page;
    size_t page_count;  /* Original request to alloc_pages, not a Buddy order. */
    list_entry_t link;
};

static struct slub_cache slub_caches[SLUB_CACHE_COUNT];
static list_entry_t slub_large_allocations;

/* Cache layer: initialize without pages; select smallest fitting class;
 * allocate via a nonfull slab; return an object and reclaim an empty slab.
 */
static void slub_cache_init(void);
static struct slub_cache *slub_select_cache(size_t size);
static void *slub_cache_alloc(struct slub_cache *cache);
static void slub_cache_free(struct slub_slab *slab, void *ptr);

/* Slab layer: find ownership without dereferencing ptr; create/register one
 * page; destroy/unregister an empty slab; take/return one checked slot.
 */
static struct slub_slab *slub_find_slab(const void *ptr);
static struct slub_slab *slub_create_slab(struct slub_cache *cache);
static void slub_destroy_slab(struct slub_slab *slab);
static void *slub_take_object(struct slub_slab *slab);
static void slub_return_object(struct slub_slab *slab, void *ptr);
/* Large path: allocate header + payload; free exact registered payload only.
 * A nonmatching pointer returns false without changing any allocation.
 */
static void *slub_alloc_large(size_t size);
static bool slub_free_large(void *ptr);
static size_t slub_capacity(const struct slub_slab *slab);

static void
slub_init(void) {
    list_init(&slub_large_allocations);
    slub_cache_init();
}

static void *
slub_alloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    if (size <= SLUB_MAX_SMALL) {
        struct slub_cache *cache = slub_select_cache(size);
        assert(cache != NULL);
        return slub_cache_alloc(cache);
    }
    return slub_alloc_large(size);
}

static void
slub_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    struct slub_slab *slab = slub_find_slab(ptr);
    if (slab != NULL) {
        slub_cache_free(slab, ptr);
    } else {
        bool released = slub_free_large(ptr);
        assert(released);
    }
}

/* lab2 2413646: byte manager, not a replacement for pmm_manager. */
const struct kmem_manager slub_kmem_manager = {
    .name = "slub_kmem_manager",
    .init = slub_init,
    .alloc = slub_alloc,
    .free = slub_free,
#ifdef SLUB_TEST
    .check = slub_check,
#else
    .check = NULL,
#endif
};

/* lab2 2413646: cache metadata is small, static, and separate from slabs. */
static bool
slub_valid_cache(const struct slub_cache *cache) {
    for (size_t i = 0; i < SLUB_CACHE_COUNT; i++) {
        if (cache == &slub_caches[i]) {
            return 1;
        }
    }
    return 0;
}

static void
slub_cache_init(void) {
    size_t size = SLUB_ALIGNMENT;
    for (size_t i = 0; i < SLUB_CACHE_COUNT; i++, size <<= 1) {
        slub_caches[i].object_size = size;
        list_init(&slub_caches[i].slabs);
    }
}

static struct slub_cache *
slub_select_cache(size_t size) {
    if (size == 0 || size > SLUB_MAX_SMALL) {
        return NULL;
    }
    for (size_t i = 0; i < SLUB_CACHE_COUNT; i++) {
        if (size <= slub_caches[i].object_size) {
            return &slub_caches[i];
        }
    }
    return NULL;
}

static void *
slub_cache_alloc(struct slub_cache *cache) {
    assert(slub_valid_cache(cache));
    list_entry_t *head = &cache->slabs;
    for (list_entry_t *le = list_next(head); le != head; le = list_next(le)) {
        struct slub_slab *slab = to_struct(le, struct slub_slab, link);
        assert(slab->cache == cache && slab->inuse <= slub_capacity(slab));
        if (slab->inuse < slub_capacity(slab)) {
            return slub_take_object(slab);
        }
    }
    struct slub_slab *slab = slub_create_slab(cache);
    return slab == NULL ? NULL : slub_take_object(slab);
}

static void
slub_cache_free(struct slub_slab *slab, void *ptr) {
    assert(slab != NULL && slub_valid_cache(slab->cache));
    slub_return_object(slab, ptr);
    if (slab->inuse == 0) {
        /* destroy owns unlinking; do not touch the slab after this call. */
        slub_destroy_slab(slab);
    }
}

/* lab2 2413646: only fixed, small metadata sizes are rounded here. */
static size_t
slub_header_size(size_t size) {
    return (size + SLUB_ALIGNMENT - 1) & ~((size_t)SLUB_ALIGNMENT - 1);
}

/* Fixed one-page layout makes capacity derivable; do not store a copy. */
static size_t
slub_capacity(const struct slub_slab *slab) {
    return (PGSIZE - slub_header_size(sizeof(*slab))) / slab->cache->object_size;
}

static char *
slub_object_start(struct slub_slab *slab) {
    return (char *)slab + slub_header_size(sizeof(*slab));
}

/* A slab is already trusted through its registered list. Check ptr using
 * integer addresses before accessing an object's contents or bitmap bit.
 */
static size_t
slub_slot_index(struct slub_slab *slab, const void *ptr) {
    size_t size = slab->cache->object_size;
    uintptr_t start = (uintptr_t)slub_object_start(slab);
    uintptr_t addr = (uintptr_t)ptr;
    assert(addr >= start);
    uintptr_t offset = addr - start;
    assert(offset < slub_capacity(slab) * size);
    assert(offset % size == 0);
    return offset / size;
}

static struct slub_slab *
slub_find_slab(const void *ptr) {
    uintptr_t addr = (uintptr_t)ptr;
    for (size_t i = 0; i < SLUB_CACHE_COUNT; i++) {
        list_entry_t *head = &slub_caches[i].slabs;
        for (list_entry_t *le = list_next(head); le != head; le = list_next(le)) {
            struct slub_slab *slab = to_struct(le, struct slub_slab, link);
            uintptr_t start = (uintptr_t)slub_object_start(slab);
            if (addr >= start && addr - start <
                    slub_capacity(slab) * slab->cache->object_size) {
                return slab;
            }
        }
    }
    return NULL;
}

static struct slub_slab *
slub_create_slab(struct slub_cache *cache) {
    assert(slub_valid_cache(cache));
    size_t offset = slub_header_size(sizeof(struct slub_slab));
    assert(offset < PGSIZE && cache->object_size >= sizeof(void *));
    size_t capacity = (PGSIZE - offset) / cache->object_size;
    assert(capacity > 0 && capacity <= SLUB_BITMAP_WORDS * 64);
    struct Page *page = alloc_pages(1);
    if (page == NULL) {
        return NULL;
    }

    struct slub_slab *slab = (void *)(page2pa(page) + va_pa_offset);
    memset(slab, 0, sizeof(*slab));
    slab->cache = cache;
    slab->page = page;
    char *start = slub_object_start(slab);
    for (size_t i = 0; i < capacity; i++) {
        void *next = i + 1 < capacity ? start + (i + 1) * cache->object_size : NULL;
        *(void **)(start + i * cache->object_size) = next;
    }
    slab->freelist = start;
    list_add(&cache->slabs, &slab->link);
    return slab;
}

static void
slub_destroy_slab(struct slub_slab *slab) {
    /* Validate registration before dereferencing even an internal argument. */
    bool registered = 0;
    for (size_t i = 0; i < SLUB_CACHE_COUNT && !registered; i++) {
        list_entry_t *head = &slub_caches[i].slabs;
        for (list_entry_t *le = list_next(head); le != head; le = list_next(le)) {
            if (to_struct(le, struct slub_slab, link) == slab) {
                registered = 1;
                break;
            }
        }
    }
    assert(registered);
    assert(slab->inuse == 0);
    for (size_t i = 0; i < SLUB_BITMAP_WORDS; i++) {
        assert(slab->allocated[i] == 0);
    }
    struct Page *page = slab->page;
    list_del_init(&slab->link);
    free_pages(page, 1);
    /* No slab access after the backing page has been returned. */
}

static void *
slub_take_object(struct slub_slab *slab) {
    assert(slab->inuse < slub_capacity(slab) && slab->freelist != NULL);
    void *object = slab->freelist;
    size_t index = slub_slot_index(slab, object);
    uint64_t mask = (uint64_t)1 << (index % 64);
    assert((slab->allocated[index / 64] & mask) == 0);
    void *next = *(void **)object;
    if (next != NULL) {
        size_t next_index = slub_slot_index(slab, next);
        assert(next != object);
        assert((slab->allocated[next_index / 64] &
                ((uint64_t)1 << (next_index % 64))) == 0);
    }
    slab->freelist = next;
    slab->allocated[index / 64] |= mask;
    slab->inuse++;
    return object;
}

static void
slub_return_object(struct slub_slab *slab, void *ptr) {
    size_t index = slub_slot_index(slab, ptr);
    uint64_t mask = (uint64_t)1 << (index % 64);
    assert(slab->inuse > 0 && slab->inuse <= slub_capacity(slab));
    assert((slab->allocated[index / 64] & mask) != 0);
    /* All input validation precedes modification. Only this slot changes. */
    slab->allocated[index / 64] &= ~mask;
    slab->inuse--;
    *(void **)ptr = slab->freelist;
    slab->freelist = ptr;
}

static void *
slub_alloc_large(size_t size) {
    size_t header = slub_header_size(sizeof(struct slub_large));
    if (size > (size_t)-1 - header || npage < nbase) {
        return NULL;
    }
    size_t bytes = size + header;
    /* Division first avoids overflow from adding PGSIZE - 1. */
    size_t count = bytes / PGSIZE + (bytes % PGSIZE != 0);
    if (count == 0 || count > npage - nbase) {
        return NULL;
    }
    struct Page *page = alloc_pages(count);
    if (page == NULL) {
        return NULL;
    }
    struct slub_large *large = (void *)(page2pa(page) + va_pa_offset);
    large->page = page;
    large->page_count = count;
    list_add(&slub_large_allocations, &large->link);
    return (char *)large + header;
}

static bool
slub_free_large(void *ptr) {
    list_entry_t *head = &slub_large_allocations;
    for (list_entry_t *le = list_next(head); le != head; le = list_next(le)) {
        struct slub_large *large = to_struct(le, struct slub_large, link);
        if ((uintptr_t)ptr != (uintptr_t)large + slub_header_size(sizeof(*large))) {
            continue;
        }
        struct Page *page = large->page;
        size_t count = large->page_count;
        list_del_init(&large->link);
        free_pages(page, count);
        return 1; /* Do not read large, le or its next link after free. */
    }
    return 0;
}
