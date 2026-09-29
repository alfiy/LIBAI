#ifndef LIBAI_KBD_H
#define LIBAI_KBD_H

#include <stdint.h>

#define LIBAI_IRQ_KBD_VECTOR 33
#define LIBAI_KBD_TEST_KEYS  3
#define LIBAI_KBD_WAIT_TICKS 500

void kbd_init(void);
void kbd_interrupt(void);
int kbd_pop(uint8_t *scancode);
char kbd_scancode_to_ascii(uint8_t scancode);

#endif
