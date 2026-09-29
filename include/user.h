#ifndef LIBAI_USER_H
#define LIBAI_USER_H

#include <stdint.h>

#define LIBAI_INT_SYSCALL 0x80
#define LIBAI_SYS_PING    42
#define LIBAI_SYS_EXIT    1

void user_init(uint64_t kernel_rsp0);
void user_run(void);
int user_on_syscall(uint64_t rax, uint64_t cs);
uint64_t user_kernel_rip(void);
uint64_t user_kernel_rsp(void);

#endif