#include "kbd.h"
#include "io.h"

#define KBD_DATA 0x60
#define KBD_BUF_SIZE 32

static uint8_t kbd_buf[KBD_BUF_SIZE];
static volatile uint64_t kbd_head;
static volatile uint64_t kbd_tail;

void
kbd_init(void)
{
    kbd_head = 0;
    kbd_tail = 0;

    /* Drop any stale byte so the first IRQ is a real key. */
    (void)inb(KBD_DATA);
}

void
kbd_interrupt(void)
{
    uint8_t sc = inb(KBD_DATA);
    uint64_t next = kbd_head + 1;

    if ((next - kbd_tail) > KBD_BUF_SIZE) {
        return;
    }

    kbd_buf[kbd_head % KBD_BUF_SIZE] = sc;
    kbd_head = next;
}

int
kbd_pop(uint8_t *scancode)
{
    if (kbd_tail == kbd_head) {
        return 0;
    }

    *scancode = kbd_buf[kbd_tail % KBD_BUF_SIZE];
    kbd_tail++;
    return 1;
}

char
kbd_scancode_to_ascii(uint8_t scancode)
{
    static const char map[0x40] = {
        [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
        [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
        [0x0A] = '9', [0x0B] = '0',
        [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
        [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
        [0x18] = 'o', [0x19] = 'p',
        [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
        [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
        [0x26] = 'l',
        [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
        [0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
        [0x0E] = '\b',
        [0x1C] = '\n',
        [0x39] = ' ',
    };

    if (scancode & 0x80) {
        return 0;
    }

    if (scancode >= 0x40) {
        return 0;
    }

    return map[scancode];
}
