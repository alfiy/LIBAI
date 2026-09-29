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

static uint64_t *vmm_pml4;
static uint64_t *vmm_pdpt;
static uint64_t *vmm_pd;

static void
vmm_load_cr3(uint64_t cr3)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

static uint64_t
vmm_addr_mask(void)
{
    return ~0xFFFull;
}

static int
vmm_split_large_page(uint64_t *pd, uint64_t pd_index)
{
    uint64_t entry = pd[pd_index];
    uint64_t phys_base;
    uint64_t *pt;
    uint64_t i;

    if ((entry & LIBAI_VMM_PAGE_PRESENT) == 0) {
        return 0;
    }

    if ((entry & LIBAI_VMM_PAGE_LARGE) == 0) {
        return 1;
    }

    pt = vmm_alloc_table();
    if (pt == 0) {
        return 0;
    }

    phys_base = entry & 0x000FFFFFFFE00000ull;

    for (i = 0; i < 512; i++) {
        pt[i] = (phys_base + (i * LIBAI_PAGE_SIZE)) |
                LIBAI_VMM_PAGE_PRESENT |
                LIBAI_VMM_PAGE_WRITE;
    }

    pd[pd_index] = (uint64_t)(uintptr_t)pt |
                   LIBAI_VMM_PAGE_PRESENT |
                   LIBAI_VMM_PAGE_WRITE |
                   LIBAI_VMM_PAGE_USER;

    return 1;
}

int
vmm_allow_user(uint64_t virt)
{
    uint64_t pd_index;
    uint64_t pt_index;
    uint64_t *pt;

    if (vmm_pd == 0 || virt >= LIBAI_PMM_MAX_PHYS) {
        return 0;
    }

    vmm_pml4[0] |= LIBAI_VMM_PAGE_USER;
    vmm_pdpt[0] |= LIBAI_VMM_PAGE_USER;

    pd_index = virt / 0x200000ull;
    if (!vmm_split_large_page(vmm_pd, pd_index)) {
        return 0;
    }

    vmm_pd[pd_index] |= LIBAI_VMM_PAGE_USER;

    pt = (uint64_t *)(uintptr_t)(vmm_pd[pd_index] & vmm_addr_mask());
    pt_index = (virt / LIBAI_PAGE_SIZE) % 512;
    pt[pt_index] |= LIBAI_VMM_PAGE_USER;

    vmm_load_cr3((uint64_t)(uintptr_t)vmm_pml4);
    return 1;
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

    vmm_pml4 = pml4;
    vmm_pdpt = pdpt;
    vmm_pd = pd;

    info.pml4 = (uint64_t)(uintptr_t)pml4;
    info.pdpt = (uint64_t)(uintptr_t)pdpt;
    info.pd = (uint64_t)(uintptr_t)pd;
    info.cr3 = info.pml4;
    info.mapped_bytes = 512ull * 0x200000ull;

    vmm_load_cr3(info.cr3);
    return info;
}
