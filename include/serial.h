#ifndef LIBAI_SERIAL_H
#define LIBAI_SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_putchar(char c);
void serial_puts(const char *s);
void serial_print_hex(uint64_t value);
void serial_print_u64(uint64_t value);
void libai_halt(void);

#endif