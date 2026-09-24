#include <stdint.h>

#include "bootinfo.h"
#include "uefi_mmap.h"

/*
 * M0.5 kernel
 *
 * After the M0.4 BootInfo handshake, walk the UEFI memory map
 * and print a usable-memory summary. No page allocator yet.
 */

volatile uint8_t libai_bss_buffer[4096];

#define COM1 0x3F8
#define LIBAI_PAGE_SIZE 4096ull

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

static void
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

static void
serial_putchar(char c)
{
    if (c == '\n') {
        serial_putchar('\r');
    }

    while ((inb(COM1 + 5) & 0x20) == 0) {
    }

    outb(COM1 + 0, (uint8_t)c);
}

static void
serial_puts(const char *s)
{
    while (*s) {
        serial_putchar(*s++);
    }
}

static void
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

static void
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

static void
halt(void)
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
        halt();
    }

    if ((map_size % desc_size) != 0) {
        serial_puts("[ERROR] Memory map size is not a multiple of descriptor size.\n");
        halt();
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
        halt();
    }

    serial_puts("[M0.4] BootInfo at ");
    serial_print_hex((uint64_t)(uintptr_t)info);
    serial_puts("\n");

    serial_puts("[M0.4] magic      = ");
    serial_print_hex(info->magic);
    serial_puts("\n");

    if (info->magic != LIBAI_BOOTINFO_MAGIC) {
        serial_puts("[ERROR] Invalid BootInfo magic.\n");
        halt();
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
        halt();
    }

    desc_count =
        info->memory_map_size / info->memory_map_descriptor_size;

    serial_puts("[M0.4] desc count = ");
    serial_print_u64(desc_count);
    serial_puts("\n");

    serial_puts("[M0.4] BootInfo handshake successful.\n");

    dump_memory_map(info);

    halt();
}
