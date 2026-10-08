
ENTRY(libai_kernel_entry)

/*
 * M0.17: kernel VMA lives in the higher half.
 * Physical load address (LMA) stays at 1 MiB so the UEFI
 * loader can still copy bytes with identity mapping.
 *
 *   virt = phys + 0xFFFFFFFF80000000
 *
 * GNU ld wants:  .section ALIGN(n) : AT(lma)
 * not:           .section : ALIGN(n) AT(lma)
 */

KERNEL_VMA_BASE = 0xFFFFFFFF80000000;

SECTIONS
{
    . = 0xFFFFFFFF80100000;
    __kernel_start = .;

    .text : AT(ADDR(.text) - KERNEL_VMA_BASE)
    {
        *(.text*)
    }

    .rodata ALIGN(0x1000) : AT(ADDR(.rodata) - KERNEL_VMA_BASE)
    {
        *(.rodata*)
    }

    .data ALIGN(0x1000) : AT(ADDR(.data) - KERNEL_VMA_BASE)
    {
        *(.data*)
    }

    .bss ALIGN(0x1000) : AT(ADDR(.bss) - KERNEL_VMA_BASE)
    {
        *(.bss*)
        *(COMMON)
    }

    . = ALIGN(0x1000);
    __kernel_end = .;
}