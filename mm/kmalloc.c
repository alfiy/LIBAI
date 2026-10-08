#include "kmalloc.h"
#include "pmm.h"
#include "vmm.h"

/*
 * M0.20: a tiny kernel heap on top of the page allocator.
 *
 * PMM hands out 4 KiB physical pages. kmalloc cuts a page into
 * smaller blocks and returns a higher-half pointer, so the kernel
 * never writes the heap through the low identity window.
 *
 *   [ header | payload | header | payload | ... ]
 *
 * Free blocks sit on one singly linked list. This is not a slab
 * allocator; it only has to prove split, free, and reuse.
 */

struct kblk {
    uint64_t size;
    struct kblk *next;
    uint64_t used;
};

#define KBLK_ALIGN 16ull

static struct kblk *kheap_free;

static uint64_t
kalign(uint64_t n)
{
    return (n + (KBLK_ALIGN - 1)) & ~(KBLK_ALIGN - 1);
}

static void *
kphys_to_ptr(uint64_t phys)
{
    return (void *)(uintptr_t)vmm_to_higher(phys);
}

void *
kmalloc(uint64_t size)
{
    struct kblk *prev = 0;
    struct kblk *blk;
    uint64_t need;
    uint64_t phys;
    uint8_t *page;

    size = kalign(size == 0 ? 1 : size);
    need = sizeof(struct kblk) + size;

    for (blk = kheap_free; blk != 0; prev = blk, blk = blk->next) {
        if (blk->size < size) {
            continue;
        }
        if (prev == 0) {
            kheap_free = blk->next;
        } else {
            prev->next = blk->next;
        }
        blk->used = 1;
        blk->next = 0;
        return (uint8_t *)blk + sizeof(struct kblk);
    }

    if (need > LIBAI_PAGE_SIZE) {
        return 0;
    }

    phys = pmm_alloc_page();
    if (phys == 0) {
        return 0;
    }

    page = kphys_to_ptr(phys);
    blk = (struct kblk *)page;
    blk->size = LIBAI_PAGE_SIZE - sizeof(struct kblk);
    blk->next = 0;
    blk->used = 1;

    if (blk->size >= size + sizeof(struct kblk) + KBLK_ALIGN) {
        struct kblk *rest = (struct kblk *)((uint8_t *)blk + sizeof(struct kblk) + size);
        rest->size = blk->size - size - sizeof(struct kblk);
        rest->used = 0;
        rest->next = kheap_free;
        kheap_free = rest;
        blk->size = size;
    }

    return (uint8_t *)blk + sizeof(struct kblk);
}

void
kfree(void *ptr)
{
    struct kblk *blk;

    if (ptr == 0) {
        return;
    }

    blk = (struct kblk *)((uint8_t *)ptr - sizeof(struct kblk));
    blk->used = 0;
    blk->next = kheap_free;
    kheap_free = blk;
}

int
kmalloc_self_test(void)
{
    uint64_t *a;
    uint64_t *b;
    uint64_t *c;
    uint64_t first;

    a = kmalloc(32);
    b = kmalloc(64);
    if (a == 0 || b == 0) {
        return 0;
    }
    if ((uint64_t)(uintptr_t)a < LIBAI_HH_BASE ||
        (uint64_t)(uintptr_t)b < LIBAI_HH_BASE) {
        return 0;
    }

    a[0] = 0x19;
    b[0] = 0x20;
    first = (uint64_t)(uintptr_t)a;
    kfree(a);

    c = kmalloc(32);
    if (c == 0 || (uint64_t)(uintptr_t)c != first || c[0] != 0x19) {
        return 0;
    }
    if (b[0] != 0x20) {
        return 0;
    }

    kfree(c);
    kfree(b);
    return 1;
}
