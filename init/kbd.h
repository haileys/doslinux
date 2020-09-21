#ifndef KBD_H
#define KBD_H

#include <stdint.h>

#define KBD_IRQ 1
#define KBD_SCANCODE_BUFFER_SIZE 16
#define KBD_PORT_LO 0x60
#define KBD_PORT_HI 0x64

typedef struct kbd {
    uint8_t scanbuff[KBD_SCANCODE_BUFFER_SIZE];
    size_t scanbuff_len;
}
kbd_t;

void
kbd_init(kbd_t* kbd);

uint8_t
kbd_read_port(kbd_t* kbd, uint16_t port);

void
kbd_write_port(kbd_t* kbd, uint16_t port, uint8_t value);

void
kbd_send_input(kbd_t* kbd, uint8_t scancode);

#endif
