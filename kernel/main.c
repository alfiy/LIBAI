#include <stdint.h>

#include "bootinfo.h"
#include "uefi_mmap.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"
#include "idt.h"
#include "irq.h"
#include "kbd.h"
#include "gdt.h"
#include "user.h"
#include "kmalloc.h"
#include "task.h"

/*
 * M0.12 kernel
 *
 * After IRQ0/IRQ1 come up, drop into a serial+keyboard shell.
 * The old automatic #PF test is now the "pf" command.
 */

volatile uint8_t libai_bss_buffer[4096];

#define COM1 0x3F8

static inline void
outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1"
                      :
                      : "a"(value), "Nd"(port));
}

static inline uint8_t
inb(uint16_t port)
{
    uint8_t value;

    __asm__ volatile ("inb %1, %0"
                      : "=a"(value)
                      : "Nd"(port));

    return value;
}

void
serial_init(void)
{
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

void
serial_putchar(char c)
{
    if (c == '\n') {
        serial_putchar('\r');
    }

    while ((inb(COM1 + 5) & 0x20) == 0) {
    }

    outb(COM1 + 0, (uint8_t)c);
}

void
serial_puts(const char *s)
{
    while (*s) {
        serial_putchar(*s++);
    }
}

void
serial_print_hex(uint64_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[16];
    int i;

    serial_puts("0x");

    for (i = 0; i < 16; i++) {
        buf[15 - i] = hex[value & 0xF];
        value >>= 4;
    }

    for (i = 0; i < 16; i++) {
        serial_putchar(buf[i]);
    }
}

void
serial_print_u64(uint64_t value)
{
    char buf[20];
    int n = 0;

    if (value == 0) {
        serial_putchar('0');
        return;
    }

    while (value > 0 && n < 20) {
        buf[n++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (n > 0) {
        serial_putchar(buf[--n]);
    }
}

void
libai_halt(void)
{
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void
dump_memory_map(const LibaiBootInfo *info)
{
    uint64_t desc_size = info->memory_map_descriptor_size;
    uint64_t map_size = info->memory_map_size;
    uint8_t *map = (uint8_t *)(uintptr_t)info->memory_map;
    uint64_t offset;
    uint64_t index = 0;

    uint64_t pages_by_type[LIBAI_EFI_TYPE_COUNT];
    uint64_t count_by_type[LIBAI_EFI_TYPE_COUNT];
    uint64_t unknown_pages = 0;
    uint64_t unknown_count = 0;
    uint64_t usable_pages = 0;
    uint64_t reserved_pages = 0;
    uint64_t acpi_reclaim_pages = 0;
    uint32_t t;

    if (desc_size < sizeof(LibaiEfiMemoryDescriptor)) {
        serial_puts("[ERROR] Descriptor smaller than known layout.\n");
        libai_halt();
    }

    if ((map_size % desc_size) != 0) {
        serial_puts("[ERROR] Memory map size is not a multiple of descriptor size.\n");
        libai_halt();
    }

    for (t = 0; t < LIBAI_EFI_TYPE_COUNT; t++) {
        pages_by_type[t] = 0;
        count_by_type[t] = 0;
    }

    serial_puts("\n");
    serial_puts("[M0.5] Walking UEFI memory map...\n");

    for (offset = 0; offset < map_size; offset += desc_size) {
        LibaiEfiMemoryDescriptor *d =
            (LibaiEfiMemoryDescriptor *)(map + offset);
        uint64_t bytes = d->number_of_pages * LIBAI_PAGE_SIZE;

        serial_puts("[M0.5] #");
        serial_print_u64(index);
        serial_puts("  ");
        serial_puts(libai_efi_type_name(d->type));
        serial_puts("  phys=");
        serial_print_hex(d->physical_start);
        serial_puts("  pages=");
        serial_print_u64(d->number_of_pages);
        serial_puts("  bytes=");
        serial_print_u64(bytes);
        serial_puts("\n");

        if (d->type < LIBAI_EFI_TYPE_COUNT) {
            pages_by_type[d->type] += d->number_of_pages;
            count_by_type[d->type] += 1;
        } else {
            unknown_pages += d->number_of_pages;
            unknown_count += 1;
        }

        if (libai_efi_type_usable_now(d->type)) {
            usable_pages += d->number_of_pages;
        } else if (d->type == LIBAI_EFI_ACPI_RECLAIM_MEMORY) {
            acpi_reclaim_pages += d->number_of_pages;
        } else {
            reserved_pages += d->number_of_pages;
        }

        index++;
    }

    serial_puts("\n");
    serial_puts("[M0.5] Pages by type:\n");

    for (t = 0; t < LIBAI_EFI_TYPE_COUNT; t++) {
        if (count_by_type[t] == 0) {
            continue;
        }

        serial_puts("        ");
        serial_puts(libai_efi_type_name(t));
        serial_puts(": ");
        serial_print_u64(count_by_type[t]);
        serial_puts(" regions, ");
        serial_print_u64(pages_by_type[t]);
        serial_puts(" pages (");
        serial_print_u64(pages_by_type[t] * LIBAI_PAGE_SIZE);
        serial_puts(" bytes)\n");
    }

    if (unknown_count > 0) {
        serial_puts("        Unknown: ");
        serial_print_u64(unknown_count);
        serial_puts(" regions, ");
        serial_print_u64(unknown_pages);
        serial_puts(" pages\n");
    }

    serial_puts("\n");
    serial_puts("[M0.5] Usable now (Loader/BS/Conventional): ");
    serial_print_u64(usable_pages);
    serial_puts(" pages / ");
    serial_print_u64(usable_pages * LIBAI_PAGE_SIZE);
    serial_puts(" bytes\n");

    serial_puts("[M0.5] ACPI reclaim later: ");
    serial_print_u64(acpi_reclaim_pages);
    serial_puts(" pages / ");
    serial_print_u64(acpi_reclaim_pages * LIBAI_PAGE_SIZE);
    serial_puts(" bytes\n");

    serial_puts("[M0.5] Reserved / runtime / MMIO: ");
    serial_print_u64(reserved_pages);
    serial_puts(" pages / ");
    serial_print_u64(reserved_pages * LIBAI_PAGE_SIZE);
    serial_puts(" bytes\n");

    serial_puts("[M0.5] Memory map walk successful.\n");
}

static void
print_pmm_stats(const char *tag)
{
    LibaiPmmStats s = pmm_stats();

    serial_puts(tag);
    serial_puts(" free=");
    serial_print_u64(s.free_pages);
    serial_puts(" used=");
    serial_print_u64(s.used_pages);
    serial_puts(" managed=");
    serial_print_u64(s.managed_pages);
    serial_puts("\n");
}

static void
test_pmm(const LibaiBootInfo *info)
{
    uint64_t rsp;
    uint64_t a;
    uint64_t b;
    uint64_t c;
    volatile uint32_t *page;

    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));

    serial_puts("\n");
    serial_puts("[M0.6] Initializing physical page allocator...\n");
    serial_puts("[M0.6] Kernel image ");
    serial_puts("reserved via __kernel_start/__kernel_end\n");
    serial_puts("[M0.6] Current stack RSP=");
    serial_print_hex(rsp);
    serial_puts("\n");

    pmm_init(info, rsp);
    print_pmm_stats("[M0.6] after init   ");

    a = pmm_alloc_page();
    b = pmm_alloc_page();

    serial_puts("[M0.6] alloc A = ");
    serial_print_hex(a);
    serial_puts("\n");
    serial_puts("[M0.6] alloc B = ");
    serial_print_hex(b);
    serial_puts("\n");

    if (a == 0 || b == 0 || a == b) {
        serial_puts("[ERROR] pmm_alloc_page() failed.\n");
        libai_halt();
    }

    page = (volatile uint32_t *)(uintptr_t)a;
    page[0] = 0x4C494241;
    page[1] = 0x00000049;

    if (page[0] != 0x4C494241) {
        serial_puts("[ERROR] allocated page is not writable.\n");
        libai_halt();
    }

    serial_puts("[M0.6] wrote marker into page A\n");
    print_pmm_stats("[M0.6] after alloc  ");

    pmm_free_page(a);
    serial_puts("[M0.6] freed A\n");
    print_pmm_stats("[M0.6] after free A ");

    c = pmm_alloc_page();
    serial_puts("[M0.6] alloc C = ");
    serial_print_hex(c);
    serial_puts("\n");

    if (c != a) {
        serial_puts("[M0.6] note: C != A (acceptable, depends on search hint)\n");
    } else {
        serial_puts("[M0.6] C reused A, free list works.\n");
    }

    pmm_free_page(b);
    pmm_free_page(c);
    print_pmm_stats("[M0.6] after free all");

    serial_puts("[M0.6] Physical page allocator test successful.\n");
}

static void test_vmm(void);
static void after_higher_half(void);
static void after_high_stack(uint64_t stack_phys);
static void kbd_shell(void);

void
libai_after_user(void)
{
    serial_puts("[M0.14] back in ring0, CS=");
    serial_print_hex(gdt_read_cs());
    serial_puts("\n");
    serial_puts("[M0.14] Ring3 demo successful.\n");
    kbd_shell();
}

#define KERNEL_STACK_PAGES 4

static uint64_t kernel_stack_base;
static uint64_t kernel_stack_top;
static uint64_t old_stack_page;

static void
kernel_on_new_stack(void)
{
    uint64_t rsp;
    volatile uint8_t probe[128];
    uint64_t i;

    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));

    for (i = 0; i < 128; i++) {
        probe[i] = (uint8_t)i;
    }

    serial_puts("[M0.7] now running on kernel stack\n");
    serial_puts("[M0.7] new RSP = ");
    serial_print_hex(rsp);
    serial_puts("\n");

    if (rsp < kernel_stack_base || rsp > kernel_stack_top) {
        serial_puts("[ERROR] RSP is outside the new kernel stack.\n");
        libai_halt();
    }

    if (probe[127] != 127) {
        serial_puts("[ERROR] stack probe write failed.\n");
        libai_halt();
    }

    serial_puts("[M0.7] stack probe write OK\n");

    pmm_free_page(old_stack_page);
    serial_puts("[M0.7] released old UEFI stack page ");
    serial_print_hex(old_stack_page);
    serial_puts("\n");
    print_pmm_stats("[M0.7] after switch ");

    serial_puts("[M0.7] Kernel stack switch successful.\n");
    test_vmm();
    libai_halt();
}

static void
test_vmm(void)
{
    LibaiVmmInfo v;
    uint64_t cr3_after;
    volatile uint32_t *kernel_probe;
    volatile uint8_t *stack_probe;

    serial_puts("\n");
    serial_puts("[M0.8] Installing identity page tables...\n");
    serial_puts("[M0.8] old CR3 = ");
    serial_print_hex(vmm_read_cr3());
    serial_puts("\n");

    v = vmm_init_identity();
    if (v.cr3 == 0) {
        serial_puts("[ERROR] vmm_init_identity() failed.\n");
        libai_halt();
    }

    cr3_after = vmm_read_cr3();

    serial_puts("[M0.8] PML4     = ");
    serial_print_hex(v.pml4);
    serial_puts("\n");
    serial_puts("[M0.8] PDPT     = ");
    serial_print_hex(v.pdpt);
    serial_puts("\n");
    serial_puts("[M0.8] PD       = ");
    serial_print_hex(v.pd);
    serial_puts("\n");
    serial_puts("[M0.8] new CR3  = ");
    serial_print_hex(cr3_after);
    serial_puts("\n");
    serial_puts("[M0.8] mapped   = ");
    serial_print_u64(v.mapped_bytes);
    serial_puts(" bytes\n");

    if ((cr3_after & ~0xfffull) != (v.cr3 & ~0xfffull)) {
        serial_puts("[ERROR] CR3 does not match new PML4.\n");
        libai_halt();
    }

    kernel_probe = (volatile uint32_t *)(uintptr_t)0x100000;
    serial_puts("[M0.8] kernel[0x100000] = ");
    serial_print_hex(kernel_probe[0]);
    serial_puts("\n");

    stack_probe = (volatile uint8_t *)(uintptr_t)kernel_stack_base;
    stack_probe[0] = 0xA5;
    if (stack_probe[0] != 0xA5) {
        serial_puts("[ERROR] stack page not writable after paging.\n");
        libai_halt();
    }

    serial_puts("[M0.8] stack page still writable\n");
    print_pmm_stats("[M0.8] after paging ");
    serial_puts("[M0.8] Identity map installed.\n");

    serial_puts("\n");
    serial_puts("[M0.16] Mapping higher-half alias 0xFFFFFFFF80000000...\n");
    if (!vmm_map_higher_half()) {
        serial_puts("[ERROR] higher-half map failed.\n");
        libai_halt();
    }

    {
        volatile uint32_t *low = (volatile uint32_t *)(uintptr_t)0x100000;
        volatile uint32_t *high =
            (volatile uint32_t *)(uintptr_t)vmm_to_higher(0x100000);

        serial_puts("[M0.16] low [0x100000]  = ");
        serial_print_hex(low[0]);
        serial_puts("\n");
        serial_puts("[M0.16] high[HH+1MB]    = ");
        serial_print_hex(high[0]);
        serial_puts("\n");

        if (low[0] != high[0]) {
            serial_puts("[ERROR] higher-half alias does not match.\n");
            libai_halt();
        }
    }

    serial_puts("[M0.16] low RIP = ");
    serial_print_hex(vmm_read_rip());
    serial_puts("\n");
    serial_puts("[M0.16] Jumping to higher-half RIP...\n");
    vmm_jump_higher(after_higher_half);
}

static void
after_higher_half(void)
{
    uint64_t rip = vmm_read_rip();

    serial_puts("[M0.16] high RIP = ");
    serial_print_hex(rip);
    serial_puts("\n");

    if (rip < LIBAI_HH_BASE) {
        serial_puts("[ERROR] still running in the low half.\n");
        libai_halt();
    }

    serial_puts("[M0.16] Higher-half window active.\n");
    {
        extern char __kernel_start[];
        extern char __kernel_end[];
        uint64_t kphys = vmm_virt_to_phys((uint64_t)(uintptr_t)__kernel_start);
        uint64_t kbytes = (uint64_t)(uintptr_t)(__kernel_end - __kernel_start);
        volatile uint32_t *high =
            (volatile uint32_t *)(uintptr_t)vmm_to_higher(0x100000);

        serial_puts("[M0.17] kernel linked in higher half\n");
        serial_puts("[M0.17] __kernel_start = ");
        serial_print_hex((uint64_t)(uintptr_t)__kernel_start);
        serial_puts("\n");

        serial_puts("[M0.18] Unmapping low kernel window ");
        serial_print_hex(kphys);
        serial_puts(" bytes=");
        serial_print_u64(kbytes);
        serial_puts("\n");

        if (!vmm_unmap_low_kernel(kphys, kbytes)) {
            serial_puts("[ERROR] cannot unmap low kernel window.\n");
            libai_halt();
        }

        serial_puts("[M0.18] low  0x100000 present = ");
        serial_print_u64(vmm_low_present(0x100000));
        serial_puts("\n");
        serial_puts("[M0.18] high kernel still reads ");
        serial_print_hex(high[0]);
        serial_puts("\n");

        if (vmm_low_present(0x100000) != 0) {
            serial_puts("[ERROR] low kernel page is still present.\n");
            libai_halt();
        }
    }

    {
        uint64_t rsp;
        uint64_t high_rsp;
        uint64_t stack_phys = kernel_stack_base;

        __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));
        high_rsp = vmm_to_higher(rsp);

        serial_puts("[M0.19] old RSP = ");
        serial_print_hex(rsp);
        serial_puts("\n");
        serial_puts("[M0.19] new RSP = ");
        serial_print_hex(high_rsp);
        serial_puts("\n");

        kernel_stack_base = vmm_to_higher(kernel_stack_base);
        kernel_stack_top = vmm_to_higher(kernel_stack_top);

        /*
         * RBP still points at the low frame. Unmap only after a call
         * that builds a new frame on the high stack.
         */
        __asm__ volatile (
            "mov %[sp], %%rsp\n\t"
            "xor %%rbp, %%rbp\n\t"
            "mov %[phys], %%rdi\n\t"
            "call *%[fn]\n\t"
            :
            : [sp] "r"(high_rsp),
              [phys] "r"(stack_phys),
              [fn] "r"(after_high_stack)
            : "memory"
        );
        libai_halt();
    }
}

static void
after_high_stack(uint64_t stack_phys)
{
    volatile uint32_t *slot =
        (volatile uint32_t *)(uintptr_t)kernel_stack_base;

    /*
     * Probe the bottom of the stack, not RSP-16. A write just below
     * RSP is reused by the next call and will not stay 0x19.
     */
    *slot = 0x19u;

    if (!vmm_unmap_low_kernel(stack_phys, KERNEL_STACK_PAGES * LIBAI_PAGE_SIZE)) {
        serial_puts("[ERROR] cannot unmap low kernel stack.\n");
        libai_halt();
    }

    serial_puts("[M0.19] low stack present = ");
    serial_print_u64(vmm_low_present(stack_phys));
    serial_puts("\n");
    serial_puts("[M0.19] high stack probe = ");
    serial_print_hex(*slot);
    serial_puts("\n");

    if (vmm_low_present(stack_phys) != 0 || *slot != 0x19u) {
        serial_puts("[ERROR] higher-half stack switch failed.\n");
        libai_halt();
    }

    gdt_init(kernel_stack_top);
    user_init(kernel_stack_top);
    if (gdt_read_cs() != LIBAI_GDT_KERNEL_CS) {
        serial_puts("[ERROR] CS is not kernel 0x08 after GDT reload.\n");
        libai_halt();
    }

    idt_init();
    irq_init();

    serial_puts("[M0.10] Enabling interrupts, waiting for ");
    serial_print_u64(LIBAI_IRQ_TEST_TICKS);
    serial_puts(" timer ticks...\n");

    irq_enable();
    while (irq_ticks() < LIBAI_IRQ_TEST_TICKS) {
        __asm__ volatile ("hlt");
    }
    irq_disable();

    serial_puts("[M0.10] timer ticks = ");
    serial_print_u64(irq_ticks());
    serial_puts("\n");
    serial_puts("[M0.10] PIT IRQ0 successful.\n");

    serial_puts("[M0.20] Kernel heap test...\n");
    if (!kmalloc_self_test()) {
        serial_puts("[ERROR] kmalloc self-test failed.\n");
        libai_halt();
    }
    serial_puts("[M0.20] kmalloc split, free, and reuse ok.\n");

    kbd_shell();
}

static int
streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void
kbd_shell(void)
{
    char line[64];
    uint64_t n = 0;
    uint8_t sc;
    char ch;

    serial_puts("\n");
    serial_puts("[M0.12] Libai shell. Type help + Enter.\n");
    serial_puts("[M0.12] Keys: telnet 127.0.0.1 45454 then sendkey <key>\n");
    serial_puts("[M0.12] Enter is: sendkey ret\n");
    serial_puts("libai> ");

    irq_enable();

    for (;;) {
        if (!kbd_pop(&sc)) {
            __asm__ volatile ("hlt");
            continue;
        }

        ch = kbd_scancode_to_ascii(sc);
        if (ch == 0) {
            continue;
        }

        if (ch == '\b') {
            if (n > 0) {
                n--;
                serial_puts("\b \b");
            }
            continue;
        }

        if (ch == '\n') {
            line[n] = 0;
            serial_puts("\n");

            if (n == 0) {
                /* empty line */
            } else if (streq(line, "help")) {
                serial_puts("commands: help ticks mem gdt cr3 rip win heap task preempt sleep event lock mbox user pf halt\n");
            } else if (streq(line, "ticks")) {
                serial_puts("ticks = ");
                serial_print_u64(irq_ticks());
                serial_puts("\n");
            } else if (streq(line, "mem")) {
                print_pmm_stats("pmm ");
            } else if (streq(line, "gdt")) {
                serial_puts("CS=");
                serial_print_hex(gdt_read_cs());
                serial_puts(" TR=");
                serial_print_hex(gdt_read_tr());
                serial_puts("\n");
            } else if (streq(line, "rip")) {
                serial_puts("RIP=");
                serial_print_hex(vmm_read_rip());
                serial_puts("\n");
            } else if (streq(line, "cr3")) {
                serial_puts("CR3=");
                serial_print_hex(vmm_read_cr3());
                serial_puts(" kernel=");
                serial_print_hex(vmm_kernel_cr3());
                serial_puts("\n");
            } else if (streq(line, "win")) {
                serial_puts("low 0x100000 present=");
                serial_print_u64(vmm_low_present(0x100000));
                serial_puts(" stack present=");
                serial_print_u64(vmm_low_present(vmm_virt_to_phys(kernel_stack_base)));
                serial_puts(" RSP=");
                {
                    uint64_t rsp;
                    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));
                    serial_print_hex(rsp);
                }
                serial_puts("\n");
            } else if (streq(line, "heap")) {
                if (kmalloc_self_test()) {
                    serial_puts("heap ok\n");
                } else {
                    serial_puts("heap failed\n");
                }
            } else if (streq(line, "task")) {
                task_demo();
            } else if (streq(line, "preempt")) {
                task_preempt_demo();
            } else if (streq(line, "sleep")) {
                task_sleep_demo();
            } else if (streq(line, "event")) {
                task_event_demo();
            } else if (streq(line, "lock")) {
                task_lock_demo();
            } else if (streq(line, "mbox")) {
                task_mbox_demo();
            } else if (streq(line, "user")) {
                user_run();
                serial_puts("[ERROR] user_run returned\n");
            } else if (streq(line, "pf")) {
                idt_test_page_fault();
            } else if (streq(line, "halt")) {
                serial_puts("halting.\n");
                irq_disable();
                libai_halt();
            } else {
                serial_puts("unknown command: ");
                serial_puts(line);
                serial_puts("\n");
            }

            n = 0;
            serial_puts("libai> ");
            continue;
        }

        if (n + 1 < sizeof(line) && ch >= 32 && ch < 127) {
            line[n++] = ch;
            serial_putchar(ch);
        }
    }
}

static void
switch_to_kernel_stack(void)
{
    uint64_t old_rsp;
    uint64_t base;
    uint64_t top;

    __asm__ volatile ("mov %%rsp, %0" : "=r"(old_rsp));

    old_stack_page = old_rsp & ~(LIBAI_PAGE_SIZE - 1);

    serial_puts("\n");
    serial_puts("[M0.7] Switching off the UEFI stack...\n");
    serial_puts("[M0.7] old RSP  = ");
    serial_print_hex(old_rsp);
    serial_puts("\n");
    serial_puts("[M0.7] old page = ");
    serial_print_hex(old_stack_page);
    serial_puts("\n");

    base = pmm_alloc_pages(KERNEL_STACK_PAGES);
    if (base == 0) {
        serial_puts("[ERROR] cannot allocate kernel stack.\n");
        libai_halt();
    }

    top = base + (KERNEL_STACK_PAGES * LIBAI_PAGE_SIZE);
    top &= ~0xFull;

    kernel_stack_base = base;
    kernel_stack_top = top;

    serial_puts("[M0.7] stack base = ");
    serial_print_hex(base);
    serial_puts("\n");
    serial_puts("[M0.7] stack top  = ");
    serial_print_hex(top);
    serial_puts("\n");
    serial_puts("[M0.7] stack size = ");
    serial_print_u64(KERNEL_STACK_PAGES * LIBAI_PAGE_SIZE);
    serial_puts(" bytes\n");
    print_pmm_stats("[M0.7] after alloc  ");

    {
        uint64_t fn = vmm_virt_to_phys(
            (uint64_t)(uintptr_t)kernel_on_new_stack
        );

        __asm__ volatile (
            "mov %[top], %%rsp\n\t"
            "xor %%rbp, %%rbp\n\t"
            "call *%[fn]\n\t"
            "1:\n\t"
            "hlt\n\t"
            "jmp 1b\n\t"
            :
            : [top] "r"(top),
              [fn] "r"(fn)
            : "memory"
        );
    }
}

void
libai_kernel_entry(LibaiBootInfo *info)
{
    uint64_t desc_count;

    libai_bss_buffer[0] = 0x42;

    serial_init();

    serial_puts("[M0.4] Libai Kernel started.\n");
    serial_puts("Libai is the best AI kernel\n");

    if (info == 0) {
        serial_puts("[ERROR] BootInfo pointer is NULL.\n");
        libai_halt();
    }

    serial_puts("[M0.4] BootInfo at ");
    serial_print_hex((uint64_t)(uintptr_t)info);
    serial_puts("\n");

    serial_puts("[M0.4] magic      = ");
    serial_print_hex(info->magic);
    serial_puts("\n");

    if (info->magic != LIBAI_BOOTINFO_MAGIC) {
        serial_puts("[ERROR] Invalid BootInfo magic.\n");
        libai_halt();
    }

    serial_puts("[M0.4] magic OK   = LIBAI\n");

    serial_puts("[M0.4] entry      = ");
    serial_print_hex(info->kernel_entry);
    serial_puts("\n");

    serial_puts("[M0.4] mmap       = ");
    serial_print_hex(info->memory_map);
    serial_puts("\n");

    serial_puts("[M0.4] mmap size  = ");
    serial_print_u64(info->memory_map_size);
    serial_puts(" bytes\n");

    serial_puts("[M0.4] desc size  = ");
    serial_print_u64(info->memory_map_descriptor_size);
    serial_puts(" bytes\n");

    serial_puts("[M0.4] desc ver   = ");
    serial_print_u64(info->memory_map_descriptor_version);
    serial_puts("\n");

    if (info->memory_map == 0 ||
        info->memory_map_descriptor_size == 0) {
        serial_puts("[ERROR] Memory map is missing.\n");
        libai_halt();
    }

    desc_count =
        info->memory_map_size / info->memory_map_descriptor_size;

    serial_puts("[M0.4] desc count = ");
    serial_print_u64(desc_count);
    serial_puts("\n");

    serial_puts("[M0.4] BootInfo handshake successful.\n");

    dump_memory_map(info);
    test_pmm(info);
    switch_to_kernel_stack();

    libai_halt();
}
