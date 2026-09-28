#include "vmm.h"

/*
 * M0.8: replace UEFI page tables with an identity map of 1 GiB
 * using 2 MiB large pages.
 *
 *   PML4[0] -> PDPT[0] -> PD[0..511] each covering 2 MiB
 *
 * Virtual address == physical address for 0 .. 1GiB.
 * COM1 is an I/O port, so serial still works after the CR3 load.
 */

static void
vmm_zero_page(void *page)
{
    uint64_t *p = (uint64_t *)page;
    uint64_t i;

    for (i = 0; i < (LIBAI_PAGE_SIZE / sizeof(uint64_t)); i++) {
        p[i] = 0;
    }
}

static uint64_t *
vmm_alloc_table(void)
{
    uint64_t phys = pmm_alloc_page();

    if (phys == 0) {
        return 0;
    }

    vmm_zero_page((void *)(uintptr_t)phys);
    return (uint64_t *)(uintptr_t)phys;
}

uint64_t
vmm_read_cr3(void)
{
    uint64_t cr3;

    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static void
vmm_load_cr3(uint64_t cr3)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

LibaiVmmInfo
vmm_init_identity(void)
{
    LibaiVmmInfo info;
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t i;

    info.cr3 = 0;
    info.pml4 = 0;
    info.pdpt = 0;
    info.pd = 0;
    info.mapped_bytes = 0;

    pml4 = vmm_alloc_table();
    pdpt = vmm_alloc_table();
    pd = vmm_alloc_table();

    if (pml4 == 0 || pdpt == 0 || pd == 0) {
        return info;
    }

    pml4[0] = (uint64_t)(uintptr_t)pdpt |
              LIBAI_VMM_PAGE_PRESENT |
              LIBAI_VMM_PAGE_WRITE;

    pdpt[0] = (uint64_t)(uintptr_t)pd |
              LIBAI_VMM_PAGE_PRESENT |
              LIBAI_VMM_PAGE_WRITE;

    for (i = 0; i < 512; i++) {
        pd[i] = (i * 0x200000ull) |
                LIBAI_VMM_PAGE_PRESENT |
                LIBAI_VMM_PAGE_WRITE |
                LIBAI_VMM_PAGE_LARGE;
    }

    info.pml4 = (uint64_t)(uintptr_t)pml4;
    info.pdpt = (uint64_t)(uintptr_t)pdpt;
    info.pd = (uint64_t)(uintptr_t)pd;
    info.cr3 = info.pml4;
    info.mapped_bytes = 512ull * 0x200000ull;

    vmm_load_cr3(info.cr3);
    return info;
}
