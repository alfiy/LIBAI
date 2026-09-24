#include <stdint.h>

/*
 * M0.3.4 kernel
 *
 * After ExitBootServices(), UEFI Print() is gone.
 * This file talks to QEMU's 16550 UART at COM1 (0x3F8).
 * On Ubuntu: qemu-system-x86_64 -nographic -serial mon:stdio
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
    outb(COM1 + 1, 0x00);    /* disable UART interrupts */
    outb(COM1 + 3, 0x80);    /* DLAB on */
    outb(COM1 + 0, 0x01);    /* divisor low: 115200 */
    outb(COM1 + 1, 0x00);    /* divisor high */
    outb(COM1 + 3, 0x03);    /* 8N1 */
    outb(COM1 + 2, 0xC7);    /* FIFO enable */
    outb(COM1 + 4, 0x0B);    /* RTS / DTR */
}

static void
serial_putchar(char c)
{
    if (c == '\n') {
        serial_putchar('\r');
    }

    while ((inb(COM1 + 5) & 0x20) == 0) {
        /* wait until THR is empty */
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

void
libai_kernel_entry(void)
{
    libai_bss_buffer[0] = 0x42;

    serial_init();
    serial_puts("[M0.3.4] Libai Kernel started.\n");
    serial_puts("Libai is the best AI kernel\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
