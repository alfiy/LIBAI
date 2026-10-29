#include "syscall.h"
#include "irq.h"
#include "kbd.h"
#include "serial.h"
#include "user.h"

static int
copy_from_user(char *kdst, uint64_t usrc, uint64_t len)
{
    uint64_t i;

    if (!user_range_ok(usrc, len)) {
        serial_puts("[M0.31] reject ptr=");
        serial_print_hex(usrc);
        serial_puts(" len=");
        serial_print_u64(len);
        serial_puts("\n");
        return -1;
    }
    for (i = 0; i < len; i++) {
        kdst[i] = ((const char *)(uintptr_t)usrc)[i];
    }
    serial_puts("[M0.31] copied ");
    serial_print_u64(len);
    serial_puts(" from ");
    serial_print_hex(usrc);
    serial_puts("\n");
    return 0;
}

typedef int64_t (*libai_sys_fn)(uint64_t arg0, uint64_t arg1, uint64_t arg2);

static int64_t
sys_read(uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    uint8_t sc;
    char ch;

    (void)arg0;
    (void)arg1;
    (void)arg2;
    serial_puts("[M0.30] read waiting for one key\n");
    for (;;) {
        if (!kbd_read(&sc)) {
            return -1;
        }
        ch = kbd_scancode_to_ascii(sc);
        if (ch != 0) {
            break;
        }
    }
    serial_puts("[M0.30] read got '");
    serial_putchar(ch);
    serial_puts("'\n");
    return (int64_t)(uint8_t)ch;
}

static int64_t
sys_write(uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    char buf[64];
    uint64_t i;

    (void)arg2;
    if (arg1 == 0 || arg1 > sizeof(buf)) {
        serial_puts("[M0.31] write bad length\n");
        return -1;
    }
    if (copy_from_user(buf, arg0, arg1) != 0) {
        return -1;
    }
    for (i = 0; i < arg1; i++) {
        serial_putchar(buf[i]);
    }
    return (int64_t)arg1;
}

static int64_t
sys_sleep(uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    uint64_t until;

    (void)arg1;
    (void)arg2;
    until = irq_ticks() + arg0;
    serial_puts("[M0.30] sleep ");
    serial_print_u64(arg0);
    serial_puts(" ticks until ");
    serial_print_u64(until);
    serial_puts("\n");
    while (irq_ticks() < until) {
        __asm__ volatile ("sti; hlt");
    }
    serial_puts("[M0.30] sleep done at ");
    serial_print_u64(irq_ticks());
    serial_puts("\n");
    return 0;
}

static int64_t
sys_yield(uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    (void)arg0;
    (void)arg1;
    (void)arg2;
    serial_puts("[M0.30] yield\n");
    return 0;
}

static int64_t
sys_exit(uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    (void)arg0;
    (void)arg1;
    (void)arg2;
    serial_puts("[M0.30] exit\n");
    return 0;
}

static const libai_sys_fn syscall_table[LIBAI_SYS_COUNT] = {
    sys_read,
    sys_write,
    sys_sleep,
    sys_yield,
    sys_exit,
};

static const char *syscall_name[LIBAI_SYS_COUNT] = {
    "read",
    "write",
    "sleep",
    "yield",
    "exit",
};

int
syscall_dispatch(uint64_t nr, uint64_t arg0, uint64_t arg1,
                 uint64_t arg2, int64_t *ret)
{
    serial_puts("[M0.30] syscall ");
    serial_print_u64(nr);
    if (nr < LIBAI_SYS_COUNT) {
        serial_puts(" ");
        serial_puts(syscall_name[nr]);
    }
    serial_puts("\n");

    if (nr >= LIBAI_SYS_COUNT) {
        serial_puts("[M0.30] unknown syscall\n");
        *ret = -1;
        return 1;
    }

    *ret = syscall_table[nr](arg0, arg1, arg2);
    return nr == LIBAI_SYS_EXIT;
}
