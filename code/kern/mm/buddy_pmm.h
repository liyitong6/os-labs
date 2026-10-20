#ifndef __KERN_MM_BUDDY_PMM_H__
#define __KERN_MM_BUDDY_PMM_H__

#include <pmm.h>

/* lab2 2413646: block-head Buddy initialization, allocation and release.
 *
 * property records actual block pages at free/allocated heads. PageProperty
 * marks free heads; an internal flag marks allocated heads. Non-heads carry
 * neither identity. No private per-page state, requested count or order is
 * stored; list indices are computed from the page count when needed.
 *
 * Allocation rounds n up to a power of two. Release accepts any positive n
 * that rounds to the recorded actual block size (e.g. 3 or 4 for a 4-page
 * allocation). Partial release, invalid heads and double release are rejected.
 * Callers must stop using the whole block and remove all mappings/references.
 * Normal allocation/release never scan or reset the block's interior pages.
 *
 * Select the manager with PMM=buddy. Its check callback remains NULL until
 * the separate test module installs real checks.
 */

/* lab2 2413646: public manager; allocation metadata stays private. */
extern const struct pmm_manager buddy_pmm_manager;

#endif /* !__KERN_MM_BUDDY_PMM_H__ */
