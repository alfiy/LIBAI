#ifndef LIBAI_GDT_H
#define LIBAI_GDT_H

#include <stdint.h>

#define LIBAI_GDT_NULL_SEL   0x00
#define LIBAI_GDT_KERNEL_CS  0x08
#define LIBAI_GDT_KERNEL_DS  0x10
#define LIBAI_GDT_TSS_SEL    0x18

void gdt_init(uint64_t kernel_rsp0);
uint16_t gdt_read_cs(void);
uint16_t gdt_read_tr(void);

#endif
