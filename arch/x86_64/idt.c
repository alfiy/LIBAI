#include <stdint.h>

#include "idt.h"
#include "irq.h"
#include "serial.h"
#include "gdt.h"
#include "user.h"
#include "vmm.h"
#include "task.h"

/*
 * M0.10: exceptions 0..31 still halt after a dump.
 * IRQ stubs 32..47 return through iretq after irq_handle().
 */

struct IdtEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct IdtPtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct IsrFrame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

#define IDT_GATE_INTERRUPT 0x8E

static struct IdtEntry idt[256] __attribute__((aligned(16)));

static const char *exception_name(uint64_t vector)
{
    switch (vector) {
    case 0:  return "Divide Error";
    case 1:  return "Debug";
    case 2:  return "NMI";
    case 3:  return "Breakpoint";
    case 4:  return "Overflow";
    case 5:  return "Bound Range";
    case 6:  return "Invalid Opcode";
    case 7:  return "Device Not Available";
    case 8:  return "Double Fault";
    case 10: return "Invalid TSS";
    case 11: return "Segment Not Present";
    case 12: return "Stack Fault";
    case 13: return "General Protection";
    case 14: return "Page Fault";
    case 16: return "x87 FP";
    case 17: return "Alignment Check";
    case 18: return "Machine Check";
    case 19: return "SIMD FP";
    default: return "Exception";
    }
}

static uint64_t
read_cr2(void)
{
    uint64_t value;

    __asm__ volatile ("mov %%cr2, %0" : "=r"(value));
    return value;
}

static uint16_t
read_cs(void)
{
    uint16_t cs;

    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    return cs;
}

static void
idt_set_gate(uint8_t vector, uint64_t handler, uint16_t cs, uint8_t type)
{
    idt[vector].offset_low = (uint16_t)(handler & 0xFFFF);
    idt[vector].selector = cs;
    idt[vector].ist = 0;
    idt[vector].type_attr = type;
    idt[vector].offset_mid = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[vector].offset_high = (uint32_t)(handler >> 32);
    idt[vector].reserved = 0;
}

void
isr_dispatch(struct IsrFrame *frame)
{
    if (frame->vector >= 32 && frame->vector < 48) {
        irq_handle(frame->vector);
        return;
    }

    if (frame->vector == LIBAI_INT_SYSCALL) {
        if (user_on_syscall(frame->rax, frame->cs)) {
            frame->rip = user_kernel_rip();
            frame->cs = LIBAI_GDT_KERNEL_CS;
            frame->ss = LIBAI_GDT_KERNEL_DS;
            frame->rsp = user_kernel_rsp();
            frame->rflags = 0x202;
        }
        return;
    }

    serial_puts("\n");
    serial_puts("[M0.9] CPU exception #");
    serial_print_u64(frame->vector);
    serial_puts(" ");
    serial_puts(exception_name(frame->vector));
    serial_puts("\n");

    serial_puts("        RIP    = ");
    serial_print_hex(frame->rip);
    serial_puts("\n");
    serial_puts("        CS     = ");
    serial_print_hex(frame->cs);
    serial_puts("\n");
    serial_puts("        RFLAGS = ");
    serial_print_hex(frame->rflags);
    serial_puts("\n");
    serial_puts("        RSP    = ");
    serial_print_hex(frame->rsp);
    serial_puts("\n");
    serial_puts("        error  = ");
    serial_print_hex(frame->error);
    serial_puts("\n");

    if (frame->vector == 14) {
        uint64_t cr2 = read_cr2();
        uint64_t err = frame->error;

        serial_puts("        CR2    = ");
        serial_print_hex(cr2);
        serial_puts("\n");
        serial_puts("        PF: ");
        serial_puts((err & 1) ? "protection " : "not-present ");
        serial_puts((err & 2) ? "write " : "read ");
        serial_puts((err & 4) ? "user " : "supervisor ");
        if (err & 8) {
            serial_puts("reserved-bit ");
        }
        if (err & 16) {
            serial_puts("instruction-fetch ");
        }
        serial_puts("\n");
        serial_puts("[M0.9] Page fault captured.\n");
    }

    serial_puts("[M0.9] System halted.\n");
    libai_halt();
}

asm (
    ".text\n"
    ".global isr_common_stub\n"
    "isr_common_stub:\n"
    "    push %rax\n"
    "    push %rbx\n"
    "    push %rcx\n"
    "    push %rdx\n"
    "    push %rsi\n"
    "    push %rdi\n"
    "    push %rbp\n"
    "    push %r8\n"
    "    push %r9\n"
    "    push %r10\n"
    "    push %r11\n"
    "    push %r12\n"
    "    push %r13\n"
    "    push %r14\n"
    "    push %r15\n"
    "    mov %rsp, %rdi\n"
    "    call isr_dispatch\n"
    "    pop %r15\n"
    "    pop %r14\n"
    "    pop %r13\n"
    "    pop %r12\n"
    "    pop %r11\n"
    "    pop %r10\n"
    "    pop %r9\n"
    "    pop %r8\n"
    "    pop %rbp\n"
    "    pop %rdi\n"
    "    pop %rsi\n"
    "    pop %rdx\n"
    "    pop %rcx\n"
    "    pop %rbx\n"
    "    pop %rax\n"
    "    add $16, %rsp\n"
    "    iretq\n"
);

#define ISR_NOERR(n) \
    asm ( \
        ".text\n" \
        ".global isr_stub_" #n "\n" \
        "isr_stub_" #n ":\n" \
        "    pushq $0\n" \
        "    pushq $" #n "\n" \
        "    jmp isr_common_stub\n" \
    ); \
    void isr_stub_##n(void);

#define ISR_ERR(n) \
    asm ( \
        ".text\n" \
        ".global isr_stub_" #n "\n" \
        "isr_stub_" #n ":\n" \
        "    pushq $" #n "\n" \
        "    jmp isr_common_stub\n" \
    ); \
    void isr_stub_##n(void);

ISR_NOERR(0)  ISR_NOERR(1)  ISR_NOERR(2)  ISR_NOERR(3)
ISR_NOERR(4)  ISR_NOERR(5)  ISR_NOERR(6)  ISR_NOERR(7)
ISR_ERR(8)    ISR_NOERR(9)  ISR_ERR(10)   ISR_ERR(11)
ISR_ERR(12)   ISR_ERR(13)   ISR_ERR(14)   ISR_NOERR(15)
ISR_NOERR(16) ISR_ERR(17)   ISR_NOERR(18) ISR_NOERR(19)
ISR_NOERR(20) ISR_ERR(21)   ISR_NOERR(22) ISR_NOERR(23)
ISR_NOERR(24) ISR_NOERR(25) ISR_NOERR(26) ISR_NOERR(27)
ISR_NOERR(28) ISR_NOERR(29) ISR_NOERR(30) ISR_NOERR(31)
ISR_NOERR(32) ISR_NOERR(33) ISR_NOERR(34) ISR_NOERR(35)
ISR_NOERR(36) ISR_NOERR(37) ISR_NOERR(38) ISR_NOERR(39)
ISR_NOERR(40) ISR_NOERR(41) ISR_NOERR(42) ISR_NOERR(43)
ISR_NOERR(44) ISR_NOERR(45) ISR_NOERR(46) ISR_NOERR(47)
ISR_NOERR(128)

void
idt_init(void)
{
    struct IdtPtr ptr;
    uint16_t cs = read_cs();
    uint64_t i;

    typedef void (*stub_fn)(void);
    stub_fn stubs[48] = {
        isr_stub_0,  isr_stub_1,  isr_stub_2,  isr_stub_3,
        isr_stub_4,  isr_stub_5,  isr_stub_6,  isr_stub_7,
        isr_stub_8,  isr_stub_9,  isr_stub_10, isr_stub_11,
        isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15,
        isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19,
        isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23,
        isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27,
        isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31,
        isr_stub_32, isr_stub_33, isr_stub_34, isr_stub_35,
        isr_stub_36, isr_stub_37, isr_stub_38, isr_stub_39,
        isr_stub_40, isr_stub_41, isr_stub_42, isr_stub_43,
        isr_stub_44, isr_stub_45, isr_stub_46, isr_stub_47
    };

    for (i = 0; i < 256; i++) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].ist = 0;
        idt[i].type_attr = 0;
        idt[i].offset_mid = 0;
        idt[i].offset_high = 0;
        idt[i].reserved = 0;
    }

    for (i = 0; i < 48; i++) {
        idt_set_gate(
            (uint8_t)i,
            vmm_to_higher((uint64_t)(uintptr_t)stubs[i]),
            cs,
            IDT_GATE_INTERRUPT
        );
    }

    idt_set_gate(
        LIBAI_INT_SYSCALL,
        vmm_to_higher((uint64_t)(uintptr_t)isr_stub_128),
        cs,
        0xEE
    );

    ptr.limit = (uint16_t)(sizeof(idt) - 1);
    ptr.base = (uint64_t)(uintptr_t)idt;

    serial_puts("[M0.9] Loading IDT, CS=");
    serial_print_hex(cs);
    serial_puts(" IDT=");
    serial_print_hex(ptr.base);
    serial_puts("\n");

    __asm__ volatile ("lidt %0" : : "m"(ptr) : "memory");
    serial_puts("[M0.9] IDT loaded.\n");
}

void
idt_test_page_fault(void)
{
    volatile uint8_t *unmapped = (volatile uint8_t *)(uintptr_t)0x40000000ull;

    serial_puts("[M0.9] Triggering test page fault at 0x40000000...\n");
    *unmapped = 0x21;
    serial_puts("[ERROR] page fault did not fire.\n");
    libai_halt();
}
