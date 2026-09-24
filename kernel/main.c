#include <stdint.h>

#include "bootinfo.h"

/*
 * M0.4 kernel
 *
 * Receives LibaiBootInfo * in rdi (SysV ABI).
 * After ExitBootServices(), only serial I/O is used.
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
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    serial_puts("[M0.4] BootInfo at ");
    serial_print_hex((uint64_t)(uintptr_t)info);
    serial_puts("\n");

    serial_puts("[M0.4] magic      = ");
    serial_print_hex(info->magic);
    serial_puts("\n");

    if (info->magic != LIBAI_BOOTINFO_MAGIC) {
        serial_puts("[ERROR] Invalid BootInfo magic.\n");
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    serial_puts("[M0.4] magic OK   = LIBA\n");

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
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    desc_count =
        info->memory_map_size / info->memory_map_descriptor_size;

    serial_puts("[M0.4] desc count = ");
    serial_print_u64(desc_count);
    serial_puts("\n");

    serial_puts("[M0.4] BootInfo handshake successful.\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
