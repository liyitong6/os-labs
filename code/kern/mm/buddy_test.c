#include <buddy_test.h>
#include <buddy_pmm.h>
#include <stdio.h>
#include <string.h>

/* lab2 2413646: real-page tests, separate from the allocator implementation.
 * Large bookkeeping lives in kernel BSS and is excluded by page_init.
 * No test resets the allocator or creates fake Page/physical-memory arrays.
 */
#define TEST_PAGE_CAPACITY ((PHYSICAL_MEMORY_END - DRAM_BASE) / PGSIZE)

static struct Page *held[TEST_PAGE_CAPACITY];
static struct Page *pool_base;
static size_t pool_pages;
static struct Page *fixture_base;
static unsigned int passed_groups;

static size_t
page_index(struct Page *page) {
    uintptr_t address = (uintptr_t)page;
    uintptr_t first = (uintptr_t)pages;
    assert(address >= first);
    assert((address - first) % sizeof(struct Page) == 0);
    size_t index = (address - first) / sizeof(struct Page);
    assert(index < npage - nbase && index < TEST_PAGE_CAPACITY);
    return index;
}

static void
check_block(struct Page *page, size_t expected) {
    assert(page != NULL);
    size_t index = page_index(page);
    size_t first = page_index(pool_base);
    assert(index >= first && index - first < pool_pages);
    assert(expected <= pool_pages - (index - first));
    assert(page2ppn(page) % expected == 0);
    assert(page->property == expected && !PageProperty(page));
    for (size_t i = 0; i < expected; i++) {
        assert(!PageReserved(page + i) && !PageProperty(page + i));
        assert(page_ref(page + i) == 0);
        if (i != 0) {
            assert(page[i].property == 0);
        }
    }
}

static void
check_disjoint(struct Page *a, size_t na, struct Page *b, size_t nb) {
    uintptr_t pa = page2pa(a), pb = page2pa(b);
    assert(pa + na * PGSIZE <= pb || pb + nb * PGSIZE <= pa);
}

static void
pass_group(const char *name) {
    passed_groups++;
    cprintf("[BUDDY PASS] %s\n", name);
}

/* Start with a deliberately sparse subset of real, otherwise available RAM. */
void
buddy_test_init_memmap(struct Page *base, size_t n) {
    pool_base = base;
    pool_pages = n;
    assert(n >= 64 && n <= TEST_PAGE_CAPACITY);
    if (BUDDY_TEST_CASE != 0) {
        pmm_manager->init_memmap(base, n);
        return;
    }
    size_t offset = (16 - page2ppn(base) % 16) % 16;
    fixture_base = base + offset;
    assert(offset + 16 <= n);
    pmm_manager->init_memmap(fixture_base + 1, 3);
    pmm_manager->init_memmap(fixture_base + 4, 3);
    pmm_manager->init_memmap(fixture_base + 7, 1);
    pmm_manager->init_memmap(fixture_base + 10, 5);
}

/* Record every returned page independently to detect duplicate allocation. */
static void
drain_pages(size_t expected) {
    memset(held, 0, sizeof(held));
    size_t count = 0;
    struct Page *page;
    while ((page = alloc_page()) != NULL) {
        check_block(page, 1);
        size_t index = page_index(page);
        assert(held[index] == NULL);
        held[index] = page;
        count++;
        assert(count <= expected);
    }
    assert(count == expected && nr_free_pages() == 0);
    assert(alloc_page() == NULL && alloc_pages(2) == NULL);
}

static void
release_held(size_t expected_free) {
    for (size_t i = 0; i < npage - nbase; i++) {
        if (held[i] != NULL) {
            free_page(held[i]);
            held[i] = NULL;
        }
    }
    assert(nr_free_pages() == expected_free);
}

static void
test_registration(void) {
    assert(nr_free_pages() == 12);
    assert(fixture_base[4].property == 4);
    assert(PageProperty(fixture_base + 4));
    assert(!PageProperty(fixture_base + 6) && fixture_base[6].property == 0);
    assert(!PageProperty(fixture_base + 7) && fixture_base[7].property == 0);
    assert(alloc_pages(8) == NULL && nr_free_pages() == 12);
    struct Page *four = alloc_pages(4);
    assert(four == fixture_base + 4);
    check_block(four, 4);
    free_pages(four, 4);
    drain_pages(12);
    size_t fixture_index = page_index(fixture_base);
    for (size_t i = 0; i < pool_pages; i++) {
        size_t index = page_index(pool_base) + i;
        bool in_fixture = index >= fixture_index + 1 && index < fixture_index + 8;
        in_fixture |= index >= fixture_index + 10 && index < fixture_index + 15;
        assert((held[index] != NULL) == in_fixture);
        assert((PageReserved(pages + index) != 0) == !in_fixture);
    }
    release_held(12);

    /* Complete registration without resetting or re-registering any page. */
    size_t prefix = fixture_base - pool_base;
    if (prefix != 0) {
        pmm_manager->init_memmap(pool_base, prefix);
    }
    pmm_manager->init_memmap(fixture_base, 1);
    pmm_manager->init_memmap(fixture_base + 8, 2);
    pmm_manager->init_memmap(fixture_base + 15, 1);
    size_t suffix = pool_pages - prefix - 16;
    if (suffix != 0) {
        pmm_manager->init_memmap(fixture_base + 16, suffix);
    }
    assert(nr_free_pages() == pool_pages);
    cprintf("[BUDDY] full pool: %lu pages\n", (unsigned long)pool_pages);
    pass_group("registration: alignment, tails, multiple ranges and holes");
}

static void
test_sizes(void) {
    const size_t requests[] = {1, 2, 3, 4, 5, 7, 8, 9, 16, 17, 31, 32};
    const size_t expected[] = {1, 2, 4, 4, 8, 8, 8, 16, 16, 32, 32, 32};
    struct Page *blocks[12];
    size_t before = nr_free_pages(), used = 0;
    for (size_t i = 0; i < 12; i++) {
        blocks[i] = alloc_pages(requests[i]);
        check_block(blocks[i], expected[i]);
        for (size_t j = 0; j < i; j++) {
            check_disjoint(blocks[i], expected[i], blocks[j], expected[j]);
        }
        used += expected[i];
        assert(nr_free_pages() == before - used);
    }
    for (size_t i = 12; i > 0; i--) {
        free_pages(blocks[i - 1], requests[i - 1]);
        used -= expected[i - 1];
        assert(nr_free_pages() == before - used);
    }
    struct Page *page = alloc_pages(3);
    check_block(page, 4);
    free_pages(page, 4);
    assert(nr_free_pages() == before);
    pass_group("sizes: rounding, alignment, non-overlap and exact accounting");
}

static void
pattern(struct Page *page, size_t count, unsigned char seed, bool write) {
    volatile unsigned char *data =
        (volatile unsigned char *)(page2pa(page) + va_pa_offset);
    for (size_t i = 0; i < count * PGSIZE; i++) {
        unsigned char expected = (unsigned char)(seed + i % 251);
        if (write) {
            data[i] = expected;
        } else {
            assert(data[i] == expected);
        }
    }
}

static void
test_data(void) {
    size_t before = nr_free_pages();
    struct Page *a = alloc_pages(3), *b = alloc_pages(5), *c = alloc_pages(9);
    check_block(a, 4);
    check_block(b, 8);
    check_block(c, 16);
    check_disjoint(a, 4, b, 8);
    check_disjoint(a, 4, c, 16);
    check_disjoint(b, 8, c, 16);
    pattern(a, 4, 11, 1);
    pattern(b, 8, 73, 1);
    pattern(c, 16, 151, 1);
    pattern(a, 4, 11, 0);
    pattern(b, 8, 73, 0);
    pattern(c, 16, 151, 0);
    free_pages(b, 5);
    b = alloc_pages(5);
    check_block(b, 8);
    pattern(b, 8, 219, 1);
    pattern(a, 4, 11, 0);
    pattern(c, 16, 151, 0);
    pattern(b, 8, 219, 0);
    free_pages(a, 3);
    free_pages(b, 8);
    free_pages(c, 9);
    assert(nr_free_pages() == before);
    pass_group("data: real physical memory isolation and reuse");
}

static void
test_large_failures(void) {
    size_t before = nr_free_pages();
    struct Page *guard = alloc_pages(3);
    check_block(guard, 4);
    pattern(guard, 4, 37, 1);
    const size_t requests[] = {(size_t)-1, (size_t)1 << 63,
                              TEST_PAGE_CAPACITY + 1, TEST_PAGE_CAPACITY};
    for (size_t i = 0; i < 4; i++) {
        assert(alloc_pages(requests[i]) == NULL);
        assert(nr_free_pages() == before - 4);
        assert(guard->property == 4);
        pattern(guard, 4, 37, 0);
    }
    free_pages(guard, 3);
    assert(nr_free_pages() == before);
    pass_group("failure: oversized unsigned requests leave state intact");
}

static void
test_exhaustion(void) {
    size_t before = nr_free_pages();
    drain_pages(before);
    size_t released = 0;
    for (size_t i = 0; i < npage - nbase; i++) {
        if (held[i] != NULL && page2ppn(held[i]) % 2 == 0) {
            free_page(held[i]);
            held[i] = NULL;
            released++;
        }
    }
    assert(released > 2 && nr_free_pages() == released);
    assert(alloc_pages(2) == NULL);
    assert(nr_free_pages() == released);
    struct Page *single = alloc_page();
    check_block(single, 1);
    assert(nr_free_pages() == released - 1);
    free_page(single);
    release_held(before);
    struct Page *large = alloc_pages(8);
    check_block(large, 8);
    free_pages(large, 8);
    assert(nr_free_pages() == before);
    pass_group("exhaustion: fragmented failure and full resource recovery");
}

static void
test_partners(void) {
    const unsigned int release_order[] = {3, 1, 7, 0, 4, 2, 6, 5};
    size_t before = nr_free_pages();
    drain_pages(before);
    struct Page *base = NULL;
    for (size_t i = page_index(pool_base); i + 8 <= page_index(pool_base) + pool_pages; i++) {
        if ((i + nbase) % 8 == 0) {
            base = pages + i;
            break;
        }
    }
    assert(base != NULL);
    size_t first = page_index(base);
    for (size_t i = 0; i < 8; i++) {
        size_t index = first + release_order[i];
        assert(held[index] == pages + index);
        free_page(held[index]);
        held[index] = NULL;
        assert(nr_free_pages() == i + 1);
    }
    for (size_t round = 0; round < 2; round++) {
        struct Page *whole = alloc_pages(8);
        assert(whole == base && nr_free_pages() == 0);
        check_block(whole, 8);
        free_pages(whole, 8);
        struct Page *a = alloc_pages(4), *b = alloc_pages(4);
        check_block(a, 4);
        check_block(b, 4);
        assert((a == base && b == base + 4) || (b == base && a == base + 4));
        struct Page *low = a < b ? a : b;
        struct Page *high = a < b ? b : a;
        free_pages(round == 0 ? low : high, 4);
        assert(nr_free_pages() == 4 && alloc_pages(8) == NULL);
        free_pages(round == 0 ? high : low, 4);
        assert(nr_free_pages() == 8);
        assert(PageProperty(base) && base->property == 8);
        assert(!PageProperty(base + 4) && base[4].property == 0);
    }

    /* Refill exactly these eight pages; all other pages remain held. */
    for (size_t i = 0; i < 8; i++) {
        struct Page *page = alloc_page();
        check_block(page, 1);
        size_t index = page_index(page);
        assert(index >= first && index < first + 8 && held[index] == NULL);
        held[index] = page;
    }
    assert(nr_free_pages() == 0);
    free_page(held[first + 1]);
    held[first + 1] = NULL;
    free_page(held[first + 2]);
    held[first + 2] = NULL;
    assert(nr_free_pages() == 2 && alloc_pages(2) == NULL);
    release_held(before);
    pass_group("partners: cascade, both release orders, stale heads and non-buddies");
}

static void
test_cycles(void) {
    const size_t requests[] = {1, 3, 5, 2, 9, 7, 4, 17};
    const size_t expected[] = {1, 4, 8, 2, 16, 8, 4, 32};
    struct Page *blocks[8];
    size_t before = nr_free_pages();
    for (size_t round = 0; round < 32; round++) {
        size_t used = 0;
        for (size_t i = 0; i < 8; i++) {
            blocks[i] = alloc_pages(requests[i]);
            check_block(blocks[i], expected[i]);
            for (size_t j = 0; j < i; j++) {
                check_disjoint(blocks[i], expected[i], blocks[j], expected[j]);
            }
            used += expected[i];
            assert(nr_free_pages() == before - used);
        }
        for (size_t i = 0; i < 8; i++) {
            size_t slot = (i * 3 + round) % 8;
            free_pages(blocks[slot], requests[slot]);
        }
        assert(nr_free_pages() == before);
    }
    pass_group("cycles: 32 mixed allocation/free rounds restore baseline");
}

static void
test_expected_panic(void) {
    cprintf("[BUDDY EXPECT PANIC] case=%d\n", BUDDY_TEST_CASE);
    if (BUDDY_TEST_CASE == 1) {
        alloc_pages(0);
    } else if (BUDDY_TEST_CASE == 2) {
        free_pages(NULL, 1);
    } else if (BUDDY_TEST_CASE == 7) {
        free_pages(pages + (npage - nbase), 1);
    } else if (BUDDY_TEST_CASE == 8) {
        assert(PageReserved(pages));
        free_pages(pages, 1);
    } else if (BUDDY_TEST_CASE == 12) {
        free_pages((struct Page *)((uintptr_t)pages - sizeof(struct Page)), 1);
    } else if (BUDDY_TEST_CASE == 14) {
        pmm_manager->init_memmap(pool_base, 0);
    } else if (BUDDY_TEST_CASE == 15) {
        pmm_manager->init_memmap(pages + (npage - nbase), 1);
    } else {
        struct Page *page = alloc_pages(3);
        check_block(page, 4);
        if (BUDDY_TEST_CASE == 3) {
            free_pages(page, 3);
            free_pages(page, 3);
        } else if (BUDDY_TEST_CASE == 4) {
            free_pages(page + 1, 1);
        } else if (BUDDY_TEST_CASE == 5) {
            free_pages(page, 2);
        } else if (BUDDY_TEST_CASE == 6) {
            free_pages((struct Page *)((uintptr_t)page + 1), 3);
        } else if (BUDDY_TEST_CASE == 9) {
            free_pages(page, 0);
        } else if (BUDDY_TEST_CASE == 10) {
            free_pages(page, (size_t)-1);
        } else if (BUDDY_TEST_CASE == 11) {
            page_ref_inc(page);
            free_pages(page, 3);
        } else if (BUDDY_TEST_CASE == 13) {
            pmm_manager->init_memmap(page, 1);
        }
    }
    cprintf("[BUDDY UNEXPECTED RETURN] case=%d\n", BUDDY_TEST_CASE);
    panic("expected Buddy assertion did not occur");
}

void
buddy_check(void) {
    assert(pmm_manager == &buddy_pmm_manager);
    if (BUDDY_TEST_CASE != 0) {
        test_expected_panic();
        return;
    }
    test_registration();
    test_sizes();
    test_data();
    test_large_failures();
    test_exhaustion();
    test_partners();
    test_cycles();
    assert(passed_groups == 7 && nr_free_pages() == pool_pages);
    cprintf("[BUDDY] ALL PASS (7 groups)\n");
}
