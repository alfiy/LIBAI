#include "irq.h"
#include "io.h"
#include "kbd.h"
#include "serial.h"
#include "task.h"
#include "task.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

#define PIT_CH0   0x40
#define PIT_CMD   0x43
#define PIT_HZ    1193182ull

static volatile uint64_t timer_ticks;

static void
pic_remap(void)
{
    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    /* Unmask IRQ0 (PIT) and IRQ1 (keyboard). */
    outb(PIC1_DATA, 0xFC);
    outb(PIC2_DATA, 0xFF);
}

static void
pic_eoi(uint64_t vector)
{
    if (vector >= 40) {
        outb(PIC2_CMD, PIC_EOI);
    }

    outb(PIC1_CMD, PIC_EOI);
}

static void
pit_init(uint32_t hz)
{
    uint32_t divisor;

    if (hz == 0) {
        hz = 100;
    }

    divisor = (uint32_t)(PIT_HZ / hz);
    if (divisor == 0) {
        divisor = 1;
    }

    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0, (uint8_t)((divisor >> 8) & 0xFF));
}

void
irq_init(void)
{
    timer_ticks = 0;
    pic_remap();
    pit_init(LIBAI_IRQ_TIMER_HZ);
    kbd_init();

    serial_puts("[M0.10] PIC remapped, PIT at ");
    serial_print_u64(LIBAI_IRQ_TIMER_HZ);
    serial_puts(" Hz, IRQ1 keyboard unmasked\n");
}

void
irq_handle(uint64_t vector)
{
    if (vector == LIBAI_IRQ_TIMER_VECTOR) {
        timer_ticks++;
        pic_eoi(vector);
        task_preempt_tick();
        return;
    } else if (vector == LIBAI_IRQ_KBD_VECTOR) {
        kbd_interrupt();
    }

    pic_eoi(vector);
}

uint64_t
irq_ticks(void)
{
    return timer_ticks;
}

void
irq_enable(void)
{
    __asm__ volatile ("sti");
}

void
irq_disable(void)
{
    __asm__ volatile ("cli");
}
