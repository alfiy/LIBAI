#ifndef LIBAI_IDT_H
#define LIBAI_IDT_H

#include <stdint.h>

void idt_init(void);
void idt_test_page_fault(void);

#endif
