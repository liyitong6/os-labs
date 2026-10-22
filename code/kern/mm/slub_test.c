#include <slub_test.h>
#include <kmalloc.h>
#include <pmm.h>
#include <stdio.h>

/* lab2 2413646: public-interface tests; static records stay off the 8 KiB stack. */
#define OBJECT_LIMIT 260
#define PAGE_LIMIT ((PHYSICAL_MEMORY_END - DRAM_BASE) / PGSIZE)
#ifndef SLUB_TEST_CASE
#define SLUB_TEST_CASE 0
#endif

struct held_object {
    void *ptr;
    size_t size;
    unsigned int tag;
};

static struct held_object objects[OBJECT_LIMIT];
static struct Page *held_pages[PAGE_LIMIT];
static size_t startup_pages;
static unsigned int generation;
static void negative_case(void);

void
slub_test_before_init(void) {
    startup_pages = nr_free_pages();
#if defined(USE_KMEM_SLUB) && (SLUB_TEST_CASE == 9 || SLUB_TEST_CASE == 10)
    negative_case();
#endif
}

static void
pass(const char *name) {
    cprintf("[SLUB PASS] %s\n", name);
}

static uint8_t
pattern(unsigned int tag, size_t offset) {
    return (uint8_t)(tag * 31 + offset * 17 + 0x5a);
}

static void
reserve(size_t index, size_t size) {
    assert(index < OBJECT_LIMIT && objects[index].ptr == NULL);
    void *ptr = kmalloc(size);
    uintptr_t start = (uintptr_t)ptr;
    assert(ptr != NULL && start % 16 == 0);
    assert(start >= KERNBASE && start < KERNTOP && size <= KERNTOP - start);
    for (size_t i = 0; i < OBJECT_LIMIT; i++) {
        if (objects[i].ptr != NULL) {
            uintptr_t other = (uintptr_t)objects[i].ptr;
            assert(start + size <= other || other + objects[i].size <= start);
        }
    }
    objects[index].ptr = ptr;
    objects[index].size = size;
    objects[index].tag = ++generation;
    volatile uint8_t *bytes = ptr;
    for (size_t i = 0; i < size; i++) {
        bytes[i] = pattern(objects[index].tag, i);
    }
}

static void
verify(void) {
    for (size_t i = 0; i < OBJECT_LIMIT; i++) {
        if (objects[i].ptr != NULL) {
            volatile uint8_t *bytes = objects[i].ptr;
            for (size_t j = 0; j < objects[i].size; j++) {
                assert(bytes[j] == pattern(objects[i].tag, j));
            }
        }
    }
}

static void
release(size_t index) {
    assert(index < OBJECT_LIMIT && objects[index].ptr != NULL);
    kfree(objects[index].ptr);
    objects[index].ptr = NULL;
}

static void
test_initialization(void) {
    assert(nr_free_pages() == startup_pages);
    assert(kmalloc(0) == NULL);
    kfree(NULL);
    assert(kmalloc((size_t)-1) == NULL && kmalloc((size_t)-16) == NULL);
    assert(kmalloc((size_t)1 << 63) == NULL);
    assert(nr_free_pages() == startup_pages);
    pass("initialization and zero requests");
}

static void
test_sizes(void) {
    /* Expected classes are explicit test data, not allocator helper results. */
    const size_t requests[] = {1,15,16,17,31,32,33,63,64,65,127,128,
                              129,255,256,257,511,512,513,1023,1024,1025,2047,2048};
    const size_t classes[] = {16,16,16,32,32,32,64,64,64,128,128,128,
                             256,256,256,512,512,512,1024,1024,1024,2048,2048,2048};
    for (size_t c = 0; c < sizeof(requests) / sizeof(requests[0]); c++) {
        size_t baseline = nr_free_pages();
        size_t count = PGSIZE / classes[c] + 1;
        size_t unique_pages = 0, min_gap = (size_t)-1;
        for (size_t i = 0; i < count; i++) {
            reserve(i, requests[c]);
            uintptr_t addr = (uintptr_t)objects[i].ptr;
            bool seen = 0;
            for (size_t j = 0; j < i; j++) {
                uintptr_t other = (uintptr_t)objects[j].ptr;
                if (addr / PGSIZE == other / PGSIZE) {
                    seen = 1;
                    size_t gap = addr > other ? addr - other : other - addr;
                    if (gap < min_gap) {
                        min_gap = gap;
                    }
                }
            }
            unique_pages += !seen;
        }
        assert(unique_pages > 1 && nr_free_pages() == baseline - unique_pages);
        if (classes[c] <= 1024) {
            /* Filling beyond one page exposes adjacent slots without assuming
             * allocation order or copying the private slab header layout. */
            assert(min_gap == classes[c]);
        } else {
            assert(unique_pages == count); /* 2048-byte slots plus metadata. */
        }
        verify();
        for (size_t i = 1; i < count; i += 2) {
            release(i);
        }
        verify();
        for (size_t i = 0; i < count; i += 2) {
            release(i);
        }
        assert(nr_free_pages() == baseline);
    }
    pass("class boundaries and cross-slab allocations");
}

static void
test_lifecycle(void) {
    size_t baseline = nr_free_pages();
    reserve(0, 64);
    reserve(1, 64);
    assert((uintptr_t)objects[0].ptr / PGSIZE == (uintptr_t)objects[1].ptr / PGSIZE);
    assert(nr_free_pages() == baseline - 1);
    release(0);
    assert(nr_free_pages() == baseline - 1); /* The other object pins the slab. */
    verify();
    reserve(0, 64);
    assert(nr_free_pages() == baseline - 1);
    release(1);
    verify();
    assert(nr_free_pages() == baseline - 1);
    release(0);
    assert(nr_free_pages() == baseline);
    pass("sharing, reuse and last-object reclamation");
}

static void
test_large(void) {
    const size_t sizes[] = {2049,3036,4095,4096,4097,6144,8192,8193};
    size_t baseline = nr_free_pages();
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        reserve(i, sizes[i]);
    }
    size_t occupied = nr_free_pages();
    assert(kmalloc((size_t)-1) == NULL && nr_free_pages() == occupied);
    verify();
    for (size_t i = 0; i < 8; i += 2) {
        release(i);
    }
    verify();
    for (size_t i = 0; i < 8; i += 2) {
        reserve(i, sizes[i]);
    }
    verify();
    for (size_t i = 8; i > 0; i--) {
        release(i - 1);
    }
    assert(nr_free_pages() == baseline);
    pass("large boundaries and cross-page data isolation");
}

static void
test_cycles(void) {
    const size_t sizes[] = {1,17,65,257,1025,2048,2049,3036,6144,8192};
    size_t baseline = nr_free_pages();
    for (size_t round = 0; round < 16; round++) {
        for (size_t i = 0; i < 10; i++) {
            reserve(i, sizes[i]);
        }
        for (size_t i = 0; i < 10; i++) {
            release((i * 3 + round) % 10);
            verify();
        }
        assert(nr_free_pages() == baseline);
    }
    pass("mixed sizes and repeated resource recovery");
}

static void
test_exhaustion(void) {
    size_t baseline = nr_free_pages(), count = 0;
    reserve(0, 64); /* Leave existing free slots while exhausting physical pages. */
    struct Page *page;
    while ((page = alloc_page()) != NULL) {
        assert(count < PAGE_LIMIT);
        held_pages[count++] = page;
        volatile uint8_t *bytes = (void *)(page2pa(page) + va_pa_offset);
        bytes[0] = 0x3c;
        bytes[PGSIZE - 1] = 0xc3;
    }
    assert(nr_free_pages() == 0 && count == baseline - 1);
    reserve(1, 64); /* Cached slots remain usable without a new page. */
    assert(nr_free_pages() == 0);
    assert(kmalloc(128) == NULL && kmalloc(8192) == NULL);
    assert(kmalloc((size_t)-1) == NULL && kmalloc((size_t)-16) == NULL);
    assert(nr_free_pages() == 0);
    verify();
    for (size_t i = count; i > 0; i--) {
        volatile uint8_t *bytes = (void *)(page2pa(held_pages[i - 1]) + va_pa_offset);
        assert(bytes[0] == 0x3c && bytes[PGSIZE - 1] == 0xc3);
        free_page(held_pages[i - 1]);
    }
    release(0);
    release(1);
    assert(nr_free_pages() == baseline);
    reserve(0, 3036);
    release(0);
    assert(nr_free_pages() == baseline);
    pass("exhaustion, failed-state preservation and page coexistence");
}

static void
negative_case(void) {
    cprintf("[SLUB EXPECT PANIC] case=%d\n", SLUB_TEST_CASE);
    if (SLUB_TEST_CASE == 8) {
        kmem_init();
    } else if (SLUB_TEST_CASE == 9) {
        (void)kmalloc(16);
    } else if (SLUB_TEST_CASE == 10 || SLUB_TEST_CASE == 11) {
        kfree((void *)1);
    } else {
        size_t size = SLUB_TEST_CASE == 5 || SLUB_TEST_CASE == 6 ? 6144 :
                      SLUB_TEST_CASE == 7 ? 2048 : 64;
        void *ptr = kmalloc(size);
        assert(ptr != NULL);
        if (SLUB_TEST_CASE == 1 || SLUB_TEST_CASE == 2 ||
                SLUB_TEST_CASE == 3 || SLUB_TEST_CASE == 12) {
            assert(kmalloc(64) != NULL); /* Keep this slab alive. */
        }
        switch (SLUB_TEST_CASE) {
        case 1: kfree(ptr); kfree(ptr); break;
        case 2: kfree((char *)ptr + 1); break;
        case 3: kfree((char *)ptr - 1); break;
        case 4: kfree((void *)KERNTOP); break;
        case 5: kfree((char *)ptr + 16); break;
        case 6: kfree(ptr); kfree(ptr); break;
        case 7: kfree(ptr); kfree(ptr); break;
        case 12: kfree((char *)ptr + 128); break; /* Fresh, unallocated slot. */
        default: panic("unknown SLUB negative case");
        }
    }
    cprintf("[SLUB UNEXPECTED RETURN] case=%d\n", SLUB_TEST_CASE);
}

void
slub_check(void) {
    if (SLUB_TEST_CASE != 0) {
        negative_case();
        return;
    }
#ifdef USE_KMEM_SLUB
    test_initialization();
    test_sizes();
    test_lifecycle();
    test_large();
    test_cycles();
    test_exhaustion();
    cprintf("[SLUB] ALL PASS (6 groups)\n");
#else
    size_t baseline = nr_free_pages();
    kmem_init();
    assert(kmalloc(0) == NULL && kmalloc(1) == NULL && kmalloc(2048) == NULL);
    assert(kmalloc(8192) == NULL && kmalloc((size_t)-1) == NULL);
    kfree(NULL);
    assert(nr_free_pages() == baseline);
    struct Page *page = alloc_page();
    assert(page != NULL);
    free_page(page);
    assert(nr_free_pages() == baseline);
    cprintf("[SLUB] DISABLED PASS\n");
#endif
}
