#ifndef LIBAI_VMM_H
#define LIBAI_VMM_H

#include <stdint.h>
#include "pmm.h"

#define LIBAI_VMM_PAGE_PRESENT  (1ull << 0)
#define LIBAI_VMM_PAGE_WRITE    (1ull << 1)
#define LIBAI_VMM_PAGE_USER     (1ull << 2)
#define LIBAI_VMM_PAGE_LARGE    (1ull << 7)

#define LIBAI_VMM_IDENTITY_SIZE LIBAI_PMM_MAX_PHYS

typedef struct {
    uint64_t cr3;
    uint64_t pml4;
    uint64_t pdpt;
    uint64_t pd;
    uint64_t mapped_bytes;
} LibaiVmmInfo;

typedef struct {
    uint64_t cr3;
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;
} LibaiAddrSpace;

LibaiVmmInfo vmm_init_identity(void);
uint64_t vmm_read_cr3(void);
void vmm_switch(uint64_t cr3);
uint64_t vmm_kernel_cr3(void);

int vmm_create_user_space(LibaiAddrSpace *space);
int vmm_space_allow_user(LibaiAddrSpace *space, uint64_t virt);

int vmm_allow_user(uint64_t virt);

#endif