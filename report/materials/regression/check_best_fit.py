#!/usr/bin/env python3
"""Run the real Best-Fit source with host-side framework stubs.

This checks list structure and allocator behavior, not RISC-V boot or page tables.
Usage: python3 check_best_fit.py --source PATH/TO/code
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

DEFS = r'''#ifndef MOCK_DEFS_H
#define MOCK_DEFS_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define to_struct(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#endif
'''
PMM = r'''#ifndef MOCK_PMM_H
#define MOCK_PMM_H
#include <assert.h>
#include <stdio.h>
#include <list.h>
#define PGSIZE 4096
struct Page { int ref; uint64_t flags; unsigned int property; list_entry_t page_link; };
typedef struct { list_entry_t free_list; unsigned int nr_free; } free_area_t;
struct pmm_manager {
 const char *name; void (*init)(void); void (*init_memmap)(struct Page *, size_t);
 struct Page *(*alloc_pages)(size_t); void (*free_pages)(struct Page *, size_t);
 size_t (*nr_free_pages)(void); void (*check)(void);
};
extern struct Page *pages;
extern size_t npage;
#define PageReserved(p) (((p)->flags & 1UL) != 0)
#define PageProperty(p) (((p)->flags & 2UL) != 0)
#define SetPageProperty(p) ((p)->flags |= 2UL)
#define ClearPageProperty(p) ((p)->flags &= ~2UL)
#define le2page(le, member) to_struct(le, struct Page, member)
static inline void set_page_ref(struct Page *p, int n) { p->ref = n; }
static inline int page_ref(struct Page *p) { return p->ref; }
static inline uintptr_t page2pa(struct Page *p) { return (uintptr_t)(p - pages) * PGSIZE; }
struct Page *alloc_pages(size_t n);
void free_pages(struct Page *p, size_t n);
size_t nr_free_pages(void);
#define alloc_page() alloc_pages(1)
#define free_page(p) free_pages(p, 1)
#define cprintf printf
#endif
'''
DRIVER = r'''#include <string.h>
#include "best_fit_pmm.c"
static struct Page storage[128];
struct Page *pages = storage;
size_t npage = 128;
struct Page *alloc_pages(size_t n) { return best_fit_alloc_pages(n); }
void free_pages(struct Page *p, size_t n) { best_fit_free_pages(p, n); }
size_t nr_free_pages(void) { return best_fit_nr_free_pages(); }
static void reset(void) {
 memset(storage, 0, sizeof(storage));
 for (size_t i = 0; i < 128; i++) { storage[i].flags = 1; list_init(&storage[i].page_link); }
 best_fit_init();
}
static int links_ok(void) {
 list_entry_t *it = &free_list;
 size_t visited = 0, total = 0;
 do {
  if (it->next->prev != it || it->prev->next != it) return 0;
  it = it->next;
  if (it != &free_list) {
   struct Page *p = le2page(it, page_link);
   if (!PageProperty(p)) return 0;
   total += p->property;
  }
  if (++visited > 129) return 0;
 } while (it != &free_list);
 return total == nr_free;
}
int main(int argc, char **argv) {
 if (argc != 2) return 2;
 reset();
 if (strcmp(argv[1], "adjacent-release") == 0) {
  best_fit_init_memmap(pages, 4);
  if (alloc_pages(4) != pages) return 2;
  free_pages(pages, 1);
  free_pages(pages + 1, 1);
  size_t before = nr_free_pages();
  struct Page *p = alloc_pages(2);
  printf("free_before=%zu, alloc_2=%s, free_after=%zu\n", before,
         p == pages ? "expected block" : p == NULL ? "NULL" : "wrong block", nr_free_pages());
  return !(before == 2 && p == pages && nr_free_pages() == 0 && links_ok());
 }
 if (strcmp(argv[1], "registration-links") == 0) {
  best_fit_init_memmap(pages, 1);
  best_fit_init_memmap(pages + 4, 1);
  int ok = links_ok();
  printf("separate registered ranges: links=%s, free=%zu\n", ok ? "valid" : "broken", nr_free_pages());
  return !(ok && nr_free_pages() == 2);
 }
 if (strcmp(argv[1], "best-fit-choice") == 0) {
  best_fit_init_memmap(pages, 16);
  if (alloc_pages(16) != pages) return 2;
  free_pages(pages, 4);
  free_pages(pages + 8, 2);
  struct Page *p = alloc_pages(2);
  int ok = p == pages + 8 && nr_free_pages() == 4 && links_ok();
  printf("smallest adequate block: %s\n", ok ? "correct" : "incorrect");
  return !ok;
 }
 if (strcmp(argv[1], "original-self-check") == 0) {
  best_fit_init_memmap(pages, 128);
  best_fit_check();
  printf("original basic_check and best_fit_check returned\n");
  return 0;
 }
 return 2;
}
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='the code directory')
    args = parser.parse_args()
    source = args.source.resolve()
    cases = ['adjacent-release', 'registration-links', 'best-fit-choice', 'original-self-check']
    passed = 0
    with tempfile.TemporaryDirectory(prefix='best-fit-host-', dir=Path.cwd()) as tmp:
        work = Path(tmp)
        (work / 'defs.h').write_text(DEFS)
        (work / 'pmm.h').write_text(PMM)
        (work / 'best_fit_pmm.h').write_text('#include <pmm.h>\n')
        (work / 'driver.c').write_text(DRIVER)
        shutil.copyfile(source / 'libs/list.h', work / 'list.h')
        shutil.copyfile(source / 'kern/mm/best_fit_pmm.c', work / 'best_fit_pmm.c')
        command = ['gcc', '-std=gnu99', '-O2', '-g', '-I', str(work), str(work / 'driver.c'), '-o', str(work / 'check')]
        build = subprocess.run(command, capture_output=True, text=True)
        if build.returncode:
            print(build.stdout + build.stderr)
            return build.returncode
        for case in cases:
            try:
                result = subprocess.run([str(work / 'check'), case], capture_output=True, text=True, timeout=5)
                ok = result.returncode == 0
                print(f'{"PASS" if ok else "FAIL"} {case}: {result.stdout.strip()}')
                if result.stderr:
                    print(result.stderr.strip())
            except subprocess.TimeoutExpired:
                ok = False
                print(f'FAIL {case}: timeout')
            passed += ok
    print(f'HOST REGRESSION: {passed}/{len(cases)} passed')
    print('Scope: real Best-Fit and list code with host stubs; not a QEMU/grade result.')
    return 0 if passed == len(cases) else 1

if __name__ == '__main__':
    raise SystemExit(main())
