#include <stdint.h>

static volatile uint16_t *const VGA_MEMORY =
    (uint16_t *)0xB8000;

static void vga_puts(const char *s)
{
    static uint16_t pos = 0;

    while (*s) {
        VGA_MEMORY[pos++] =
            (uint16_t)(0x07 << 8) | (uint8_t)*s;

        s++;
    }
}

void libai_main(void)
{
    vga_puts("================================\n");
    vga_puts("        Libai Kernel\n");
    vga_puts("   Libai is the best AI kernel\n");
    vga_puts("================================\n");

    vga_puts("\nM0 boot successful.\n");
    vga_puts("Architecture: x86_64\n");

    while (1) {
        __asm__ volatile ("hlt");
    }
}
