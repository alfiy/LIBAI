#include "vmm.h"

/*
 * Two address spaces:
 *
 *   kernel space  – identity 1 GiB, supervisor only
 *   user space    – same identity map for the kernel (still
 *                   supervisor), plus U=1 on that process's pages
 *
 * int $0x80 does NOT change CR3. The kernel must therefore be
 * executable in the user page tables at CPL=0.
 */

static uint64_t *vmm_k_pml4;
static uint64_t *vmm_k_pdpt;
static uint64_t *vmm_k_pd;
static uint64_t vmm_k_cr3;

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

void
vmm_switch(uint64_t cr3)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

uint64_t
vmm_kernel_cr3(void)
{
    return vmm_k_cr3;
}

static int
vmm_fill_identity(uint64_t **pml4_out, uint64_t **pdpt_out, uint64_t **pd_out)
{
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t i;

    pml4 = vmm_alloc_table();
    pdpt = vmm_alloc_table();
    pd = vmm_alloc_table();

    if (pml4 == 0 || pdpt == 0 || pd == 0) {
        return 0;
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

    *pml4_out = pml4;
    *pdpt_out = pdpt;
    *pd_out = pd;
    return 1;
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
                   LIBAI_VMM_PAGE_WRITE;

    return 1;
}

int
vmm_space_allow_user(LibaiAddrSpace *space, uint64_t virt)
{
    uint64_t pd_index;
    uint64_t pt_index;
    uint64_t *pt;

    if (space == 0 || space->pd == 0 || virt >= LIBAI_PMM_MAX_PHYS) {
        return 0;
    }

    space->pml4[0] |= LIBAI_VMM_PAGE_USER;
    space->pdpt[0] |= LIBAI_VMM_PAGE_USER;

    pd_index = virt / 0x200000ull;
    if (!vmm_split_large_page(space->pd, pd_index)) {
        return 0;
    }

    space->pd[pd_index] |= LIBAI_VMM_PAGE_USER;

    pt = (uint64_t *)(uintptr_t)(space->pd[pd_index] & ~0xFFFull);
    pt_index = (virt / LIBAI_PAGE_SIZE) % 512;
    pt[pt_index] |= LIBAI_VMM_PAGE_USER;

    return 1;
}

int
vmm_create_user_space(LibaiAddrSpace *space)
{
    if (space == 0) {
        return 0;
    }

    if (!vmm_fill_identity(&space->pml4, &space->pdpt, &space->pd)) {
        return 0;
    }

    space->cr3 = (uint64_t)(uintptr_t)space->pml4;
    return 1;
}

int
vmm_allow_user(uint64_t virt)
{
    LibaiAddrSpace kernel;

    kernel.cr3 = vmm_k_cr3;
    kernel.pml4 = vmm_k_pml4;
    kernel.pdpt = vmm_k_pdpt;
    kernel.pd = vmm_k_pd;

    if (!vmm_space_allow_user(&kernel, virt)) {
        return 0;
    }

    vmm_switch(vmm_k_cr3);
    return 1;
}

LibaiVmmInfo
vmm_init_identity(void)
{
    LibaiVmmInfo info;

    info.cr3 = 0;
    info.pml4 = 0;
    info.pdpt = 0;
    info.pd = 0;
    info.mapped_bytes = 0;

    if (!vmm_fill_identity(&vmm_k_pml4, &vmm_k_pdpt, &vmm_k_pd)) {
        return info;
    }

    vmm_k_cr3 = (uint64_t)(uintptr_t)vmm_k_pml4;

    info.pml4 = (uint64_t)(uintptr_t)vmm_k_pml4;
    info.pdpt = (uint64_t)(uintptr_t)vmm_k_pdpt;
    info.pd = (uint64_t)(uintptr_t)vmm_k_pd;
    info.cr3 = vmm_k_cr3;
    info.mapped_bytes = 512ull * 0x200000ull;

    vmm_switch(info.cr3);
    return info;
}
