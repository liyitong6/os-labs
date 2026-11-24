#include <cow.h>
#include <vmm.h>
#include <pmm.h>
#include <sync.h>
#include <string.h>
#include <error.h>

//lab5 challenge B write-fault handler 2412933
/* The supplied virt configuration is single-hart. Interrupt exclusion protects
 * local PTE/refcount updates; it is not an SMP locking implementation.
 */
int cow_resolve_write(struct mm_struct *mm, uintptr_t addr) {
    bool intr_flag;
    int result = -E_INVAL;
    local_intr_save(intr_flag);
    if (mm == NULL || mm->pgdir == NULL || addr < USERBASE || addr >= USERTOP ||
        !user_mem_check(mm, addr, 1, 1)) {
        goto out;
    }
    pte_t *ptep = get_pte(mm->pgdir, addr, 0);
    if (ptep == NULL ||
        (*ptep & (PTE_V | PTE_U | PTE_R | PTE_COW)) !=
                     (PTE_V | PTE_U | PTE_R | PTE_COW) ||
        (*ptep & PTE_W)) {
        goto out;
    }
    struct Page *old_page = pte2page(*ptep);
    int refs = page_ref(old_page);
    if (refs <= 0) {
        goto out;
    }
    uintptr_t page_addr = ROUNDDOWN(addr, PGSIZE);
    if (refs == 1) {
        *ptep = (*ptep | PTE_W | PTE_A | PTE_D) & ~(pte_t)PTE_COW;
        tlb_invalidate(mm->pgdir, page_addr);
        result = 0;
        goto out;
    }
    struct Page *new_page = alloc_page();
    if (new_page == NULL) {
        result = -E_NO_MEM;
        goto out;
    }
    set_page_ref(new_page, 0);
    memcpy(page2kva(new_page), page2kva(old_page), PGSIZE);
    uint32_t perm = ((*ptep & 0x3ff) | PTE_W | PTE_A | PTE_D) & ~PTE_COW;
    /* page_insert must atomically replace the mapping, adjust both refs and
     * invalidate the translation; on failure it must leave the old map intact.
     */
    result = page_insert(mm->pgdir, new_page, page_addr, perm);
    if (result != 0) {
        free_page(new_page);
    }
out:
    local_intr_restore(intr_flag);
    return result;
}

int cow_prepare_write(struct mm_struct *mm, uintptr_t addr, size_t len) {
    if (len == 0) {
        return 0;
    }
    if (mm == NULL || mm->pgdir == NULL || addr < USERBASE || addr >= USERTOP ||
        len > USERTOP - addr || !user_mem_check(mm, addr, len, 1)) {
        return -E_INVAL;
    }
    bool intr_flag;
    int result = 0;
    local_intr_save(intr_flag);
    uintptr_t end = addr + len;
    for (uintptr_t va = ROUNDDOWN(addr, PGSIZE); va < end; va += PGSIZE) {
        pte_t *ptep = get_pte(mm->pgdir, va, 0);
        if (ptep == NULL || (*ptep & (PTE_V | PTE_U | PTE_R)) !=
                                   (PTE_V | PTE_U | PTE_R)) {
            result = -E_INVAL;
            break;
        }
        if (*ptep & PTE_COW) {
            result = cow_resolve_write(mm, va < addr ? addr : va);
            if (result != 0) {
                break;
            }
        } else if (!(*ptep & PTE_W)) {
            result = -E_INVAL;
            break;
        }
    }
    local_intr_restore(intr_flag);
    return result;
}
