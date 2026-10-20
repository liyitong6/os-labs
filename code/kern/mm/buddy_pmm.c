#include <buddy_pmm.h>
#include <pmm.h>
#include <list.h>
#ifdef BUDDY_TEST
#include <buddy_test.h>
#endif

/* lab2 2413646: block-head Buddy management.
 *
 * property is the actual page count at either kind of block head, and zero
 * at non-heads. PageProperty marks a free head; bit 2, unused by the current
 * framework, marks an allocated head. Reserved/reference semantics are intact.
 * There is no per-page Buddy array, original-request record or stored order.
 * Absolute PFNs define alignment and buddies: partner_pfn = pfn ^ block_pages.
 * Calls are serialized during boot, as in the existing PMM framework.
 */
#define BUDDY_PAGE_CAPACITY ((PHYSICAL_MEMORY_END - DRAM_BASE) / PGSIZE)
#define BUDDY_LIST_COUNT (sizeof(size_t) * 8)
#define BUDDY_ALLOCATED_HEAD (1UL << 2)

static list_entry_t buddy_free_lists[BUDDY_LIST_COUNT];
static size_t buddy_free_count;
static bool buddy_initialized;

/* Compute a list index temporarily; no block stores this derived value. */
static unsigned int
buddy_list_index(size_t block_pages) {
    assert(block_pages > 0 && (block_pages & (block_pages - 1)) == 0);
    unsigned int index = 0;
    while (block_pages > 1) {
        block_pages >>= 1;
        index++;
    }
    return index;
}

/* Zero means that a request cannot be represented within the platform limit. */
static size_t
buddy_request_size(size_t n) {
    if (n == 0 || n > BUDDY_PAGE_CAPACITY) {
        return 0;
    }
    size_t block_pages = 1;
    while (block_pages < n) {
        if (block_pages > BUDDY_PAGE_CAPACITY / 2) {
            return 0;
        }
        block_pages <<= 1;
    }
    return block_pages;
}

/* Validate numerically before dereferencing/subtracting a supplied pointer. */
static size_t
buddy_page_index(struct Page *base) {
    assert(pages != NULL && base != NULL && npage >= nbase);
    size_t page_count = npage - nbase;
    assert(page_count <= BUDDY_PAGE_CAPACITY);
    uintptr_t array_addr = (uintptr_t)pages;
    uintptr_t base_addr = (uintptr_t)base;
    assert(base_addr >= array_addr);
    uintptr_t byte_offset = base_addr - array_addr;
    assert(byte_offset % sizeof(struct Page) == 0);
    size_t index = byte_offset / sizeof(struct Page);
    assert(index < page_count);
    return index;
}

static void
buddy_init(void) {
    for (unsigned int index = 0; index < BUDDY_LIST_COUNT; index++) {
        list_init(&buddy_free_lists[index]);
    }
    buddy_free_count = 0;
    buddy_initialized = 1;
}

static void
buddy_add_block(struct Page *base, size_t block_pages) {
    assert(!PageReserved(base) && !PageProperty(base));
    assert((base->flags & BUDDY_ALLOCATED_HEAD) == 0);
    base->property = block_pages;
    SetPageProperty(base);
    list_add(&buddy_free_lists[buddy_list_index(block_pages)], &base->page_link);
}

/* Invalidate the old free head, including when it becomes a block interior. */
static void
buddy_remove_block(struct Page *base) {
    assert(PageProperty(base));
    assert((base->flags & BUDDY_ALLOCATED_HEAD) == 0);
    list_del_init(&base->page_link);
    ClearPageProperty(base);
    base->property = 0;
}

/* Coalesce free heads. Only the caller accounts for newly available pages. */
static void
buddy_register_block(struct Page *base, size_t block_pages) {
    size_t page_count = npage - nbase;
    while (block_pages <= BUDDY_PAGE_CAPACITY / 2) {
        ppn_t partner_pfn = page2ppn(base) ^ block_pages;
        if (partner_pfn < nbase || partner_pfn >= npage) {
            break;
        }
        size_t partner_index = partner_pfn - nbase;
        if (block_pages > page_count - partner_index) {
            break;
        }
        struct Page *partner = pages + partner_index;
        if (PageReserved(partner) || !PageProperty(partner) ||
            (partner->flags & BUDDY_ALLOCATED_HEAD) != 0 ||
            partner->property != block_pages) {
            break;
        }
        buddy_remove_block(partner);
        if (partner_pfn < page2ppn(base)) {
            base = partner;
        }
        block_pages <<= 1;
    }
    buddy_add_block(base, block_pages);
}

static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(buddy_initialized && n > 0);
    size_t index = buddy_page_index(base);
    size_t page_count = npage - nbase;
    assert(n <= page_count - index);

    /* Registration alone visits each page to establish its initial state.
     * Unregistered gaps stay reserved; a second registration is rejected.
     */
    for (size_t i = 0; i < n; i++) {
        assert(PageReserved(base + i));
    }
    for (size_t i = 0; i < n; i++) {
        struct Page *page = base + i;
        ClearPageReserved(page);
        ClearPageProperty(page);
        page->flags &= ~BUDDY_ALLOCATED_HEAD;
        page->property = 0;
        set_page_ref(page, 0);
        list_init(&page->page_link);
    }

    size_t remaining = n;
    struct Page *cursor = base;
    while (remaining != 0) {
        size_t block_pages = 1;
        ppn_t pfn = page2ppn(cursor);
        while (block_pages <= remaining / 2 &&
               block_pages <= BUDDY_PAGE_CAPACITY / 2 &&
               (pfn & ((block_pages << 1) - 1)) == 0) {
            block_pages <<= 1;
        }
        buddy_register_block(cursor, block_pages);
        cursor += block_pages;
        remaining -= block_pages;
    }
    buddy_free_count += n;
}

static struct Page *
buddy_alloc_pages(size_t n) {
    assert(buddy_initialized && n > 0);
    size_t wanted_pages = buddy_request_size(n);
    if (wanted_pages == 0 || wanted_pages > buddy_free_count) {
        return NULL;
    }

    size_t available_pages = wanted_pages;
    unsigned int index = buddy_list_index(wanted_pages);
    while (list_empty(&buddy_free_lists[index])) {
        if (available_pages > BUDDY_PAGE_CAPACITY / 2) {
            return NULL;
        }
        available_pages <<= 1;
        index++;
    }

    struct Page *base = le2page(list_next(&buddy_free_lists[index]), page_link);
    assert(!PageReserved(base) && page_ref(base) == 0);
    assert(base->property == available_pages);
    buddy_remove_block(base);
    while (available_pages > wanted_pages) {
        available_pages >>= 1;
        buddy_add_block(base + available_pages, available_pages);
    }

    /* Interiors already have no valid head record; do not rewrite them. */
    base->property = wanted_pages;
    base->flags |= BUDDY_ALLOCATED_HEAD;
    buddy_free_count -= wanted_pages;
    return base;
}

/* lab2 2413646: release a whole block using head information only.
 * n need not equal the historical request: its rounded size must equal the
 * actual block size. Interior reference counts/mappings are caller duties;
 * neither references nor the physical data area are reset here.
 */
static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(buddy_initialized && n > 0);
    size_t index = buddy_page_index(base);
    struct Page *block = pages + index;
    assert(!PageReserved(block) && !PageProperty(block));
    assert((block->flags & BUDDY_ALLOCATED_HEAD) != 0);
    size_t block_pages = block->property;
    assert(block_pages > 0 && block_pages <= BUDDY_PAGE_CAPACITY);
    assert((block_pages & (block_pages - 1)) == 0);
    size_t page_count = npage - nbase;
    assert(block_pages <= page_count - index);
    assert((page2ppn(block) & (block_pages - 1)) == 0);
    assert(buddy_request_size(n) == block_pages);
    assert(page_ref(block) == 0);
    assert(buddy_free_count <= page_count);
    assert(block_pages <= page_count - buddy_free_count);

    block->flags &= ~BUDDY_ALLOCATED_HEAD;
    block->property = 0;
    buddy_register_block(block, block_pages);
    buddy_free_count += block_pages;
}

static size_t
buddy_nr_free_pages(void) {
    assert(buddy_initialized);
    return buddy_free_count;
}

/* lab2 2413646: register the real interfaces; tests are installed separately. */
const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
#ifdef BUDDY_TEST
    .check = buddy_check,
#else
    .check = NULL,
#endif
};
