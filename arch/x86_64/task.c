#include "task.h"
#include "kmalloc.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"

/*
 * M0.21: cooperative tasks.
 *
 * A task is a TCB from kmalloc plus one kernel stack page.
 * task_yield saves callee-saved registers and RSP, then loads
 * the next task. Nobody is preempted; a task runs until it yields.
 */

struct task {
    uint64_t rsp;
    uint64_t id;
    uint64_t stack;
    struct task *next;
    void (*fn)(void);
    uint64_t turns;
};

static struct task *current;
static struct task *shell_task;
static uint64_t next_id = 1;

static void task_yield(void);
static void task_exit(void);

/*
 * rsp is the first field, so (%rdi) is prev->rsp and (%rsi) is next->rsp.
 * This must be naked: a compiler epilogue would pop the new task's
 * return address into rbp and jump to garbage.
 */
__attribute__((naked)) static void
task_switch(struct task *prev, struct task *next)
{
    __asm__ volatile (
        "movq %rsi, current(%rip)\n\t"
        "pushq %rbx\n\t"
        "pushq %rbp\n\t"
        "pushq %r12\n\t"
        "pushq %r13\n\t"
        "pushq %r14\n\t"
        "pushq %r15\n\t"
        "movq %rsp, (%rdi)\n\t"
        "movq (%rsi), %rsp\n\t"
        "popq %r15\n\t"
        "popq %r14\n\t"
        "popq %r13\n\t"
        "popq %r12\n\t"
        "popq %rbp\n\t"
        "popq %rbx\n\t"
        "ret\n\t"
    );
}

static void
task_yield(void)
{
    struct task *prev = current;
    struct task *next = prev->next;

    if (next == 0) {
        next = prev;
    }
    task_switch(prev, next);
}

static void
task_exit(void)
{
    struct task *me = current;
    struct task *scan = shell_task;

    while (scan->next != me) {
        scan = scan->next;
    }
    scan->next = me->next;
    serial_puts("[M0.21] task ");
    serial_print_u64(me->id);
    serial_puts(" exit\n");
    task_switch(me, me->next);
}

static void
task_trampoline(void)
{
    current->fn();
    task_exit();
}

static struct task *
task_spawn(void (*fn)(void))
{
    struct task *t;
    uint64_t phys;
    uint64_t *sp;
    struct task *tail;

    t = kmalloc(sizeof(*t));
    phys = pmm_alloc_page();
    if (t == 0 || phys == 0) {
        return 0;
    }

    t->id = next_id++;
    t->fn = fn;
    t->turns = 0;
    t->stack = (uint64_t)(uintptr_t)vmm_to_higher(phys);
    sp = (uint64_t *)(uintptr_t)(t->stack + LIBAI_PAGE_SIZE);

    *--sp = (uint64_t)(uintptr_t)task_trampoline;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    t->rsp = (uint64_t)(uintptr_t)sp;

    tail = shell_task;
    while (tail->next != shell_task) {
        tail = tail->next;
    }
    t->next = shell_task;
    tail->next = t;

    serial_puts("[M0.21] spawn id=");
    serial_print_u64(t->id);
    serial_puts(" tcb=");
    serial_print_hex((uint64_t)(uintptr_t)t);
    serial_puts(" stack=");
    serial_print_hex(t->stack);
    serial_puts("\n");
    return t;
}

static void
worker_a(void)
{
    uint64_t i;

    for (i = 0; i < 3; i++) {
        current->turns++;
        serial_puts("[M0.21] A turn ");
        serial_print_u64(current->turns);
        serial_puts("\n");
        task_yield();
    }
}

static void
worker_b(void)
{
    uint64_t i;

    for (i = 0; i < 3; i++) {
        current->turns++;
        serial_puts("[M0.21] B turn ");
        serial_print_u64(current->turns);
        serial_puts("\n");
        task_yield();
    }
}

void
task_demo(void)
{
    struct task shell;

    shell.id = 0;
    shell.fn = 0;
    shell.turns = 0;
    shell.stack = 0;
    shell.next = &shell;
    shell_task = &shell;
    current = &shell;

    serial_puts("[M0.21] cooperative tasks, shell id=0\n");
    if (task_spawn(worker_a) == 0 || task_spawn(worker_b) == 0) {
        serial_puts("[ERROR] task_spawn failed.\n");
        return;
    }

    while (shell.next != &shell) {
        task_yield();
    }

    serial_puts("[M0.21] both workers exited, back in shell\n");
}
