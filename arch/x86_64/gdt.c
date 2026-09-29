#include "gdt.h"
#include "serial.h"

/*
 * M0.13: kernel-owned GDT + 64-bit TSS.
 *
 *   0x00  null
 *   0x08  kernel code  (L=1)
 *   0x10  kernel data
 *   0x18  TSS (16-byte long-mode descriptor)
 */

struct GdtEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_high;
} __attribute__((packed));

struct TssDesc {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct GdtPtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct Tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb;
} __attribute__((packed));

struct GdtTable {
    struct GdtEntry null;
    struct GdtEntry kernel_code;
    struct GdtEntry kernel_data;
    struct TssDesc  tss;
} __attribute__((packed));

static struct GdtTable gdt;
static struct Tss tss;

static void
gdt_set_code64(struct GdtEntry *e)
{
    e->limit_low = 0;
    e->base_low = 0;
    e->base_mid = 0;
    e->access = 0x9A;
    e->gran = 0x20;
    e->base_high = 0;
}

static void
gdt_set_data(struct GdtEntry *e)
{
    e->limit_low = 0;
    e->base_low = 0;
    e->base_mid = 0;
    e->access = 0x92;
    e->gran = 0x00;
    e->base_high = 0;
}

static void
gdt_set_tss(struct TssDesc *e, uint64_t base, uint32_t limit)
{
    e->limit_low = (uint16_t)(limit & 0xFFFF);
    e->base_low = (uint16_t)(base & 0xFFFF);
    e->base_mid = (uint8_t)((base >> 16) & 0xFF);
    e->access = 0x89;
    e->gran = (uint8_t)((limit >> 16) & 0x0F);
    e->base_high = (uint8_t)((base >> 24) & 0xFF);
    e->base_upper = (uint32_t)(base >> 32);
    e->reserved = 0;
}

uint16_t
gdt_read_cs(void)
{
    uint16_t cs;

    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    return cs;
}

uint16_t
gdt_read_tr(void)
{
    uint16_t tr;

    __asm__ volatile ("str %0" : "=r"(tr));
    return tr;
}

static void
gdt_load_and_reload(void)
{
    struct GdtPtr ptr;

    ptr.limit = (uint16_t)(sizeof(gdt) - 1);
    ptr.base = (uint64_t)(uintptr_t)&gdt;

    __asm__ volatile (
        "lgdt %[ptr]\n\t"
        "pushq %[cs]\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "mov %[ds], %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        :
        : [ptr] "m"(ptr),
          [cs] "i"(LIBAI_GDT_KERNEL_CS),
          [ds] "r"((uint16_t)LIBAI_GDT_KERNEL_DS)
        : "rax", "memory"
    );
}

void
gdt_init(uint64_t kernel_rsp0)
{
    uint16_t old_cs = gdt_read_cs();
    uint32_t i;
    uint8_t *raw = (uint8_t *)&tss;

    for (i = 0; i < sizeof(tss); i++) {
        raw[i] = 0;
    }

    tss.rsp0 = kernel_rsp0;
    tss.iopb = sizeof(tss);

    gdt.null.limit_low = 0;
    gdt.null.base_low = 0;
    gdt.null.base_mid = 0;
    gdt.null.access = 0;
    gdt.null.gran = 0;
    gdt.null.base_high = 0;

    gdt_set_code64(&gdt.kernel_code);
    gdt_set_data(&gdt.kernel_data);
    gdt_set_tss(&gdt.tss, (uint64_t)(uintptr_t)&tss, (uint32_t)(sizeof(tss) - 1));

    serial_puts("[M0.13] Loading kernel GDT, old CS=");
    serial_print_hex(old_cs);
    serial_puts("\n");
    serial_puts("[M0.13] TSS.RSP0 = ");
    serial_print_hex(kernel_rsp0);
    serial_puts("\n");

    gdt_load_and_reload();

    __asm__ volatile ("ltr %0" : : "r"((uint16_t)LIBAI_GDT_TSS_SEL) : "memory");

    serial_puts("[M0.13] new CS = ");
    serial_print_hex(gdt_read_cs());
    serial_puts("  TR=");
    serial_print_hex(gdt_read_tr());
    serial_puts("\n");
    serial_puts("[M0.13] GDT/TSS installed.\n");
}
