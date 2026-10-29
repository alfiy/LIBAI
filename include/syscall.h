#ifndef LIBAI_SYSCALL_H
#define LIBAI_SYSCALL_H

#include <stdint.h>

#define LIBAI_SYS_READ   0
#define LIBAI_SYS_WRITE  1
#define LIBAI_SYS_SLEEP  2
#define LIBAI_SYS_YIELD  3
#define LIBAI_SYS_EXIT   4
#define LIBAI_SYS_COUNT  5

int syscall_dispatch(uint64_t nr, uint64_t arg0, uint64_t arg1,
                     uint64_t arg2, int64_t *ret);

#endif