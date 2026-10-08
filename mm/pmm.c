#include "pmm.h"
#include "uefi_mmap.h"
#include "vmm.h"

/*
 * Bitmap page allocator.
 *
 * 1 = used, 0 = free
 *
 * Init:
 *   all used
 *   mark UEFI-usable regions free
 *   reserve kernel / BootInfo / mmap / current stack / page 0
 */

extern char __kernel_start[];
extern char __kernel_end[];

static uint8_t pmm_bitmap[LIBAI_PMM_MAX_PAGES / 8];
static uint64_t pmm_free_count;
static uint64_t pmm_used_count;
static uint64_t pmm_next_hint;

static void
pmm_set_used(uint64_t page, int used)
{
    uint64_t byte = page / 8;
    uint8_t bit = (uint8_t)(1u << (page % 8));

    if (page >= LIBAI_PMM_MAX_PAGES) {
        return;
    }

    if (used) {
        pmm_bitmap[byte] |= bit;
    } else {
        pmm_bitmap[byte] &= (uint8_t)~bit;
    }
}

int
pmm_page_is_free(uint64_t phys)
{
    uint64_t page;
    uint64_t byte;
    uint8_t bit;

    if ((phys % LIBAI_PAGE_SIZE) != 0) {
        return 0;
    }

    page = phys / LIBAI_PAGE_SIZE;
    if (page >= LIBAI_PMM_MAX_PAGES) {
        return 0;
    }

    byte = page / 8;
    bit = (uint8_t)(1u << (page % 8));
    return (pmm_bitmap[byte] & bit) == 0;
}

static void
pmm_mark_range(uint64_t start, uint64_t length, int used)
{
    uint64_t addr;
    uint64_t end;

    if (length == 0) {
        return;
    }

    addr = start & ~(LIBAI_PAGE_SIZE - 1);
    end = (start + length + LIBAI_PAGE_SIZE - 1) & ~(LIBAI_PAGE_SIZE - 1);

    if (addr >= LIBAI_PMM_MAX_PHYS) {
        return;
    }

    if (end > LIBAI_PMM_MAX_PHYS) {
        end = LIBAI_PMM_MAX_PHYS;
    }

    while (addr < end) {
        pmm_set_used(addr / LIBAI_PAGE_SIZE, used);
        addr += LIBAI_PAGE_SIZE;
    }
}

static void
pmm_recount(void)
{
    uint64_t page;

    pmm_free_count = 0;
    pmm_used_count = 0;

    for (page = 0; page < LIBAI_PMM_MAX_PAGES; page++) {
        if (pmm_page_is_free(page * LIBAI_PAGE_SIZE)) {
            pmm_free_count++;
        } else {
            pmm_used_count++;
        }
    }
}

void
pmm_init(const LibaiBootInfo *info, uint64_t stack_pointer)
{
    uint64_t desc_size = info->memory_map_descriptor_size;
    uint64_t map_size = info->memory_map_size;
    uint8_t *map = (uint8_t *)(uintptr_t)info->memory_map;
    uint64_t offset;
    uint64_t i;

    for (i = 0; i < (LIBAI_PMM_MAX_PAGES / 8); i++) {
        pmm_bitmap[i] = 0xFF;
    }

    for (offset = 0; offset < map_size; offset += desc_size) {
        LibaiEfiMemoryDescriptor *d =
            (LibaiEfiMemoryDescriptor *)(map + offset);

        if (!libai_efi_type_usable_now(d->type)) {
            continue;
        }

        pmm_mark_range(
            d->physical_start,
            d->number_of_pages * LIBAI_PAGE_SIZE,
            0
        );
    }

    /* Never hand out the first page. */
    pmm_mark_range(0, LIBAI_PAGE_SIZE, 1);

    pmm_mark_range(
        vmm_virt_to_phys((uint64_t)(uintptr_t)__kernel_start),
        (uint64_t)(uintptr_t)(__kernel_end - __kernel_start),
        1
    );

    pmm_mark_range((uint64_t)(uintptr_t)info, sizeof(*info), 1);
    pmm_mark_range(info->memory_map, info->memory_map_size, 1);
    pmm_mark_range(stack_pointer, 1, 1);

    pmm_next_hint = 1;
    pmm_recount();
}

uint64_t
pmm_alloc_page(void)
{
    uint64_t page;
    uint64_t start = pmm_next_hint;

    if (pmm_free_count == 0) {
        return 0;
    }

    for (page = start; page < LIBAI_PMM_MAX_PAGES; page++) {
        if (pmm_page_is_free(page * LIBAI_PAGE_SIZE)) {
            pmm_set_used(page, 1);
            pmm_free_count--;
            pmm_used_count++;
            pmm_next_hint = page + 1;
            return page * LIBAI_PAGE_SIZE;
        }
    }

    for (page = 1; page < start; page++) {
        if (pmm_page_is_free(page * LIBAI_PAGE_SIZE)) {
            pmm_set_used(page, 1);
            pmm_free_count--;
            pmm_used_count++;
            pmm_next_hint = page + 1;
            return page * LIBAI_PAGE_SIZE;
        }
    }

    return 0;
}

uint64_t
pmm_alloc_pages(uint64_t count)
{
    uint64_t page;
    uint64_t run;
    uint64_t i;

    if (count == 0) {
        return 0;
    }

    if (count == 1) {
        return pmm_alloc_page();
    }

    if (count > pmm_free_count) {
        return 0;
    }

    for (page = 1; page + count <= LIBAI_PMM_MAX_PAGES; page++) {
        int ok = 1;

        for (run = 0; run < count; run++) {
            if (!pmm_page_is_free((page + run) * LIBAI_PAGE_SIZE)) {
                page += run;
                ok = 0;
                break;
            }
        }

        if (!ok) {
            continue;
        }

        for (i = 0; i < count; i++) {
            pmm_set_used(page + i, 1);
            pmm_free_count--;
            pmm_used_count++;
        }

        pmm_next_hint = page + count;
        return page * LIBAI_PAGE_SIZE;
    }

    return 0;
}

void
pmm_free_page(uint64_t phys)
{
    uint64_t page;

    if (phys == 0 || (phys % LIBAI_PAGE_SIZE) != 0) {
        return;
    }

    page = phys / LIBAI_PAGE_SIZE;
    if (page >= LIBAI_PMM_MAX_PAGES) {
        return;
    }

    if (pmm_page_is_free(phys)) {
        return;
    }

    pmm_set_used(page, 0);
    pmm_free_count++;
    pmm_used_count--;

    if (page < pmm_next_hint) {
        pmm_next_hint = page;
    }
}

void
pmm_free_pages(uint64_t phys, uint64_t count)
{
    uint64_t i;

    for (i = 0; i < count; i++) {
        pmm_free_page(phys + (i * LIBAI_PAGE_SIZE));
    }
}

LibaiPmmStats
pmm_stats(void)
{
    LibaiPmmStats s;

    s.managed_pages = LIBAI_PMM_MAX_PAGES;
    s.free_pages = pmm_free_count;
    s.used_pages = pmm_used_count;
    return s;
}
