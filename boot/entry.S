.section .text
.global _start
.type _start, @function

_start:
    cli

    call libai_main

.hang:
    hlt
    jmp .hang

.size _start, .-_start
