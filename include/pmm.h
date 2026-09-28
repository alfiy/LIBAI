#ifndef LIBAI_PMM_H
#define LIBAI_PMM_H

#include <stdint.h>
#include "bootinfo.h"

#define LIBAI_PAGE_SIZE 4096ull

/*
 * M0.6 tracks physical pages below 1 GiB with a static bitmap.
 * Pages above this limit are ignored (left unusable).
 */
#define LIBAI_PMM_MAX_PHYS  0x40000000ull
#define LIBAI_PMM_MAX_PAGES (LIBAI_PMM_MAX_PHYS / LIBAI_PAGE_SIZE)

typedef struct {
    uint64_t managed_pages;
    uint64_t free_pages;
    uint64_t used_pages;
} LibaiPmmStats;

void pmm_init(const LibaiBootInfo *info, uint64_t stack_pointer);
uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t phys);
int pmm_page_is_free(uint64_t phys);
LibaiPmmStats pmm_stats(void);

#endif
