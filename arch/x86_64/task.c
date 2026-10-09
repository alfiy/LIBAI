#include "task.h"
#include "kmalloc.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"
#include "irq.h"
#include "gdt.h"

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
    uint64_t wake_tick;
    int sleeping;
    int waiting;
    void *wait_obj;
    int wait_for;
};

static struct task *current;
static struct task *shell_task;
static struct task *dead;
static uint64_t next_id = 1;

static void task_yield(void);
static void task_exit(void);
static void task_reap(void);

static void
task_reap(void)
{
    while (dead != 0) {
        struct task *t = dead;
        dead = t->next;
        pmm_free_page(vmm_virt_to_phys(t->stack));
        kfree(t);
    }
}

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
    while ((next->sleeping || next->waiting) && next != prev) {
        next = next->next;
    }
    if (next->sleeping || next->waiting) {
        return;
    }
    task_switch(prev, next);
    task_reap();
}

static void
task_exit(void)
{
    struct task *me = current;
    struct task *scan = shell_task;
    struct task *next;

    while (scan->next != me) {
        scan = scan->next;
    }
    next = me->next;
    scan->next = next;
    me->next = dead;
    dead = me;
    serial_puts("[M0.21] task ");
    serial_print_u64(me->id);
    serial_puts(" exit\n");
    task_switch(me, next);
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
    t->wake_tick = 0;
    t->sleeping = 0;
    t->waiting = 0;
    t->wait_obj = 0;
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
    uint64_t rsp;

    for (i = 0; i < 3; i++) {
        current->turns++;
        __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));
        serial_puts("[M0.21] A turn ");
        serial_print_u64(current->turns);
        serial_puts(" RSP=");
        serial_print_hex(rsp);
        serial_puts("\n");
        task_yield();
    }
}

static void
worker_b(void)
{
    uint64_t i;
    uint64_t rsp;

    for (i = 0; i < 3; i++) {
        current->turns++;
        __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));
        serial_puts("[M0.21] B turn ");
        serial_print_u64(current->turns);
        serial_puts(" RSP=");
        serial_print_hex(rsp);
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
        shell_task = 0;
        current = 0;
        return;
    }

    while (shell.next != &shell) {
        task_yield();
    }
    task_reap();
    shell_task = 0;
    current = 0;

    serial_puts("[M0.21] both workers exited, stacks reclaimed\n");
}

static int preempt_on;
static uint64_t slice_ticks;
static uint64_t seen_a;
static uint64_t seen_b;

static void
spin_a(void)
{
    serial_puts("[M0.22] A entered\n");
    __asm__ volatile ("sti");
    for (;;) {
        if (current->turns != seen_a) {
            seen_a = current->turns;
            serial_puts("[M0.22] A slice ");
            serial_print_u64(seen_a);
            serial_puts("\n");
        }
        __asm__ volatile ("pause");
    }
}

static void
spin_b(void)
{
    serial_puts("[M0.22] B entered\n");
    __asm__ volatile ("sti");
    for (;;) {
        if (current->turns != seen_b) {
            seen_b = current->turns;
            serial_puts("[M0.22] B slice ");
            serial_print_u64(seen_b);
            serial_puts("\n");
        }
        __asm__ volatile ("pause");
    }
}

void
task_preempt_tick(void)
{
    struct task *next;
    struct task *scan;
    uint64_t now;

    now = irq_ticks();
    scan = shell_task;
    if (scan != 0) {
        do {
            if (scan->sleeping && now >= scan->wake_tick) {
                scan->sleeping = 0;
                serial_puts("[M0.23] wake id=");
                serial_print_u64(scan->id);
                serial_puts(" at ");
                serial_print_u64(now);
                serial_puts("\n");
            }
            scan = scan->next;
        } while (scan != shell_task);
    }

    if (!preempt_on || current == 0 || current->next == current) {
        return;
    }

    slice_ticks++;
    if (slice_ticks < 10) {
        return;
    }
    slice_ticks = 0;

    if (seen_a >= 3 && seen_b >= 3) {
        preempt_on = 0;
        if (current != shell_task) {
            serial_puts("[M0.22] timer -> 0\n");
            task_switch(current, shell_task);
        }
        return;
    }

    next = current->next;
    if (next == shell_task) {
        next = next->next;
    }
    if (next == 0 || next == shell_task) {
        return;
    }
    next->turns++;
    serial_puts("[M0.22] timer -> ");
    serial_print_u64(next->id);
    serial_puts("\n");
    task_switch(current, next);
}

void
task_preempt_demo(void)
{
    struct task shell;

    shell.id = 0;
    shell.fn = 0;
    shell.turns = 0;
    shell.stack = 0;
    shell.next = &shell;
    shell_task = &shell;
    current = &shell;
    seen_a = 0;
    seen_b = 0;
    slice_ticks = 0;

    serial_puts("[M0.22] arming timer preemption\n");
    if (task_spawn(spin_a) == 0 || task_spawn(spin_b) == 0) {
        serial_puts("[ERROR] preempt spawn failed.\n");
        shell_task = 0;
        current = 0;
        return;
    }

    preempt_on = 1;
    task_yield();
    while (preempt_on) {
        __asm__ volatile ("sti; hlt");
    }

    serial_puts("[M0.22] preemption done, back in shell\n");
    shell_task = 0;
    current = 0;
}

static void
task_sleep(uint64_t ticks)
{
    current->wake_tick = irq_ticks() + ticks;
    current->sleeping = 1;
    serial_puts("[M0.23] id=");
    serial_print_u64(current->id);
    serial_puts(" sleep until ");
    serial_print_u64(current->wake_tick);
    serial_puts("\n");
    task_yield();
}

static void
sleeper(void)
{
    serial_puts("[M0.23] sleeper start ");
    serial_print_u64(irq_ticks());
    serial_puts("\n");
    task_sleep(50);
    serial_puts("[M0.23] sleeper resumed ");
    serial_print_u64(irq_ticks());
    serial_puts("\n");
}

static void
keeper(void)
{
    uint64_t i;

    for (i = 0; i < 3; i++) {
        serial_puts("[M0.23] other task running\n");
        task_yield();
    }
}

void
task_sleep_demo(void)
{
    struct task shell;

    shell.id = 0;
    shell.fn = 0;
    shell.turns = 0;
    shell.stack = 0;
    shell.wake_tick = 0;
    shell.sleeping = 0;
    shell.next = &shell;
    shell_task = &shell;
    current = &shell;

    serial_puts("[M0.23] blocking sleep\n");
    if (task_spawn(sleeper) == 0 || task_spawn(keeper) == 0) {
        serial_puts("[ERROR] sleep spawn failed.\n");
        shell_task = 0;
        current = 0;
        return;
    }

    while (shell.next != &shell) {
        task_yield();
    }
    task_reap();
    shell_task = 0;
    current = 0;
    serial_puts("[M0.23] sleep demo done\n");
}

static void
task_wait(void)
{
    current->waiting = 1;
    serial_puts("[M0.24] id=");
    serial_print_u64(current->id);
    serial_puts(" waiting for event\n");
    task_yield();
}

static void
task_signal(void)
{
    struct task *scan = shell_task;

    if (scan == 0) {
        return;
    }
    do {
        if (scan->waiting) {
            scan->waiting = 0;
            serial_puts("[M0.24] signal id=");
            serial_print_u64(scan->id);
            serial_puts("\n");
            return;
        }
        scan = scan->next;
    } while (scan != shell_task);
    serial_puts("[M0.24] signal with nobody waiting\n");
}

static void
event_waiter(void)
{
    serial_puts("[M0.24] waiter blocks\n");
    task_wait();
    serial_puts("[M0.24] waiter resumed\n");
}

static void
event_sender(void)
{
    serial_puts("[M0.24] sender runs before signal\n");
    task_yield();
    task_signal();
    serial_puts("[M0.24] sender done\n");
}

void
task_event_demo(void)
{
    struct task shell;

    shell.id = 0;
    shell.fn = 0;
    shell.turns = 0;
    shell.stack = 0;
    shell.wake_tick = 0;
    shell.sleeping = 0;
    shell.waiting = 0;
    shell.next = &shell;
    shell_task = &shell;
    current = &shell;

    serial_puts("[M0.24] event wait\n");
    if (task_spawn(event_waiter) == 0 || task_spawn(event_sender) == 0) {
        serial_puts("[ERROR] event spawn failed.\n");
        shell_task = 0;
        current = 0;
        return;
    }

    while (shell.next != &shell) {
        task_yield();
    }
    task_reap();
    shell_task = 0;
    current = 0;
    serial_puts("[M0.24] event demo done\n");
}

struct libai_mutex {
    int locked;
};

static struct libai_mutex demo_lock;
static uint64_t shared_counter;

static void
mutex_lock(struct libai_mutex *lock)
{
    while (lock->locked) {
        current->wait_obj = lock;
        current->waiting = 1;
        serial_puts("[M0.25] id=");
        serial_print_u64(current->id);
        serial_puts(" blocks on lock\n");
        task_yield();
    }
    lock->locked = 1;
    current->wait_obj = 0;
    serial_puts("[M0.25] id=");
    serial_print_u64(current->id);
    serial_puts(" holds lock\n");
}

static void
mutex_unlock(struct libai_mutex *lock)
{
    struct task *scan = shell_task;

    lock->locked = 0;
    serial_puts("[M0.25] id=");
    serial_print_u64(current->id);
    serial_puts(" unlocks\n");
    if (scan == 0) {
        return;
    }
    do {
        if (scan->waiting && scan->wait_obj == lock) {
            scan->waiting = 0;
            scan->wait_obj = 0;
            serial_puts("[M0.25] wake waiter id=");
            serial_print_u64(scan->id);
            serial_puts("\n");
            return;
        }
        scan = scan->next;
    } while (scan != shell_task);
}

static void
lock_worker(void)
{
    mutex_lock(&demo_lock);
    shared_counter++;
    serial_puts("[M0.25] counter=");
    serial_print_u64(shared_counter);
    serial_puts("\n");
    task_yield();
    mutex_unlock(&demo_lock);
}

void
task_lock_demo(void)
{
    struct task shell;

    shell.id = 0;
    shell.fn = 0;
    shell.turns = 0;
    shell.stack = 0;
    shell.wake_tick = 0;
    shell.sleeping = 0;
    shell.waiting = 0;
    shell.wait_obj = 0;
    shell.next = &shell;
    shell_task = &shell;
    current = &shell;
    demo_lock.locked = 0;
    shared_counter = 0;

    serial_puts("[M0.25] mutex\n");
    if (task_spawn(lock_worker) == 0 || task_spawn(lock_worker) == 0) {
        serial_puts("[ERROR] lock spawn failed.\n");
        shell_task = 0;
        current = 0;
        return;
    }

    while (shell.next != &shell) {
        task_yield();
    }
    task_reap();
    shell_task = 0;
    current = 0;
    serial_puts("[M0.25] mutex demo done\n");
}

#define MBOX_WANT_DATA  1
#define MBOX_WANT_SPACE 2

struct libai_mbox {
    int full;
    uint64_t value;
};

static struct libai_mbox demo_mbox;

static void
mbox_wake(struct libai_mbox *box, int reason)
{
    struct task *scan = shell_task;

    if (scan == 0) {
        return;
    }
    do {
        if (scan->waiting && scan->wait_obj == box && scan->wait_for == reason) {
            scan->waiting = 0;
            scan->wait_obj = 0;
            scan->wait_for = 0;
            serial_puts("[M0.26] wake id=");
            serial_print_u64(scan->id);
            serial_puts(reason == MBOX_WANT_DATA ? " for data\n" : " for space\n");
            return;
        }
        scan = scan->next;
    } while (scan != shell_task);
}

static void
mbox_send(struct libai_mbox *box, uint64_t value)
{
    while (box->full) {
        current->wait_obj = box;
        current->wait_for = MBOX_WANT_SPACE;
        current->waiting = 1;
        serial_puts("[M0.26] id=");
        serial_print_u64(current->id);
        serial_puts(" blocks, box full\n");
        task_yield();
    }
    box->value = value;
    box->full = 1;
    serial_puts("[M0.26] sent ");
    serial_print_u64(value);
    serial_puts("\n");
    mbox_wake(box, MBOX_WANT_DATA);
}

static uint64_t
mbox_recv(struct libai_mbox *box)
{
    uint64_t value;

    while (!box->full) {
        current->wait_obj = box;
        current->wait_for = MBOX_WANT_DATA;
        current->waiting = 1;
        serial_puts("[M0.26] id=");
        serial_print_u64(current->id);
        serial_puts(" blocks, box empty\n");
        task_yield();
    }
    value = box->value;
    box->full = 0;
    serial_puts("[M0.26] recv ");
    serial_print_u64(value);
    serial_puts("\n");
    mbox_wake(box, MBOX_WANT_SPACE);
    return value;
}

static void
mbox_producer(void)
{
    mbox_send(&demo_mbox, 11);
    task_yield();
    mbox_send(&demo_mbox, 22);
    task_yield();
    mbox_send(&demo_mbox, 33);
}

static void
mbox_consumer(void)
{
    uint64_t a;
    uint64_t b;
    uint64_t c;

    a = mbox_recv(&demo_mbox);
    b = mbox_recv(&demo_mbox);
    c = mbox_recv(&demo_mbox);
    if (a == 11 && b == 22 && c == 33) {
        serial_puts("[M0.26] order ok\n");
    } else {
        serial_puts("[M0.26] order failed\n");
    }
}

void
task_mbox_demo(void)
{
    struct task shell;

    shell.id = 0;
    shell.fn = 0;
    shell.turns = 0;
    shell.stack = 0;
    shell.wake_tick = 0;
    shell.sleeping = 0;
    shell.waiting = 0;
    shell.wait_obj = 0;
    shell.wait_for = 0;
    shell.next = &shell;
    shell_task = &shell;
    current = &shell;
    demo_mbox.full = 0;
    demo_mbox.value = 0;

    serial_puts("[M0.26] mailbox\n");
    if (task_spawn(mbox_consumer) == 0 || task_spawn(mbox_producer) == 0) {
        serial_puts("[ERROR] mbox spawn failed.\n");
        shell_task = 0;
        current = 0;
        return;
    }

    while (shell.next != &shell) {
        task_yield();
    }
    task_reap();
    shell_task = 0;
    current = 0;
    serial_puts("[M0.26] mailbox demo done\n");
}
