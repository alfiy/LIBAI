#ifndef LIBAI_KMALLOC_H
#define LIBAI_KMALLOC_H

#include <stdint.h>

void *kmalloc(uint64_t size);
void kfree(void *ptr);
int kmalloc_self_test(void);

#endif