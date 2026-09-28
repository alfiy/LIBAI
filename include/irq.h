#ifndef LIBAI_IRQ_H
#define LIBAI_IRQ_H

#include <stdint.h>

#define LIBAI_IRQ_TIMER_VECTOR 32
#define LIBAI_IRQ_TIMER_HZ     100
#define LIBAI_IRQ_TEST_TICKS   20

void irq_init(void);
void irq_handle(uint64_t vector);
uint64_t irq_ticks(void);
void irq_enable(void);
void irq_disable(void);

#endif
