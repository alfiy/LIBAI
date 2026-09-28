ENTRY(libai_kernel_entry)

SECTIONS
{
    . = 0x100000;
    __kernel_start = .;

    .text : ALIGN(0x1000)
    {
        *(.text*)
    }

    .rodata : ALIGN(0x1000)
    {
        *(.rodata*)
    }

    .data : ALIGN(0x1000)
    {
        *(.data*)
    }

    .bss : ALIGN(0x1000)
    {
        *(.bss*)
        *(COMMON)
    }

    . = ALIGN(0x1000);
    __kernel_end = .;
}