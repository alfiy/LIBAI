#include "user.h"
#include "gdt.h"
#include "pmm.h"
#include "serial.h"

/*
 * Tiny ring3 program, written by hand as machine code:
 *
 *   mov $42, %rax
 *   int $0x80
 *   mov $1, %rax
 *   int $0x80
 */

static uint64_t user_code_page;
static uint64_t user_stack_page;
static uint64_t saved_kernel_rsp0;
static volatile int user_finished;
extern void libai_after_user(void);

void
user_init(uint64_t kernel_rsp0)
{
    saved_kernel_rsp0 = kernel_rsp0;
    user_code_page = 0;
    user_stack_page = 0;
    user_finished = 0;
}

static int
user_install_program(void)
{
    static const uint8_t prog[] = {
        0x48, 0xC7, 0xC0, 0x2A, 0x00, 0x00, 0x00, /* mov $42, %rax */
        0xCD, 0x80,                               /* int $0x80     */
        0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00, /* mov $1, %rax  */
        0xCD, 0x80                                /* int $0x80     */
    };
    uint8_t *dst;
    uint64_t i;

    if (user_code_page == 0) {
        user_code_page = pmm_alloc_page();
        user_stack_page = pmm_alloc_page();
    }

    if (user_code_page == 0 || user_stack_page == 0) {
        serial_puts("[ERROR] no pages for user program.\n");
        return 0;
    }

    dst = (uint8_t *)(uintptr_t)user_code_page;
    for (i = 0; i < sizeof(prog); i++) {
        dst[i] = prog[i];
    }

    return 1;
}

int
user_on_syscall(uint64_t rax, uint64_t cs)
{
    serial_puts("[M0.14] syscall rax=");
    serial_print_u64(rax);
    serial_puts(" from CS=");
    serial_print_hex(cs);
    serial_puts("\n");

    if ((cs & 3) != 3) {
        serial_puts("[M0.14] syscall not from ring3.\n");
        return 0;
    }

    if (rax == LIBAI_SYS_PING) {
        serial_puts("[M0.14] SYS_PING ok, returning to ring3.\n");
        return 0;
    }

    if (rax == LIBAI_SYS_EXIT) {
        serial_puts("[M0.14] SYS_EXIT, returning to ring0.\n");
        user_finished = 1;
        return 1;
    }

    serial_puts("[M0.14] unknown syscall.\n");
    return 1;
}

uint64_t
user_kernel_rip(void)
{
    return (uint64_t)(uintptr_t)libai_after_user;
}

uint64_t
user_kernel_rsp(void)
{
    return saved_kernel_rsp0;
}

void
user_run(void)
{
    uint64_t user_rip;
    uint64_t user_rsp;
    uint64_t user_cs;
    uint64_t user_ss;
    uint64_t rflags;

    if (!user_install_program()) {
        return;
    }

    user_rip = user_code_page;
    user_rsp = user_stack_page + LIBAI_PAGE_SIZE;
    user_cs = LIBAI_GDT_USER_CS | LIBAI_GDT_RPL_USER;
    user_ss = LIBAI_GDT_USER_DS | LIBAI_GDT_RPL_USER;
    rflags = 0x202;

    serial_puts("[M0.14] Entering ring3, RIP=");
    serial_print_hex(user_rip);
    serial_puts(" CS=");
    serial_print_hex(user_cs);
    serial_puts(" RSP=");
    serial_print_hex(user_rsp);
    serial_puts("\n");

    __asm__ volatile (
        "pushq %[ss]\n\t"
        "pushq %[rsp]\n\t"
        "pushq %[rflags]\n\t"
        "pushq %[cs]\n\t"
        "pushq %[rip]\n\t"
        "iretq\n\t"
        :
        : [ss] "r"(user_ss),
          [rsp] "r"(user_rsp),
          [rflags] "r"(rflags),
          [cs] "r"(user_cs),
          [rip] "r"(user_rip)
        : "memory"
    );
}
