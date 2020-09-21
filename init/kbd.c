#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "kbd.h"

#define KBD_DATA_PORT       0x60
#define KBD_STATUS_PORT     0x64

#define KBD_STATUS_HAS_DATA 0x01
#define KBD_STATUS_SYSTEM   0x04

void
kbd_init(kbd_t* kbd)
{
    memset(kbd->scanbuff, 0, sizeof(kbd->scanbuff));
    kbd->scanbuff_len = 0;
}

uint8_t
kbd_read_port(kbd_t* kbd, uint16_t port)
{
    switch (port) {
        case KBD_DATA_PORT: {
            if (kbd->scanbuff_len > 0) {
                uint8_t scancode = kbd->scanbuff[0];

                // no point using a ring buffer for such a small buffer, plus
                // moving all remaining elements is simpler code
                memmove(kbd->scanbuff, kbd->scanbuff + 1, kbd->scanbuff_len - 1);

                kbd->scanbuff_len--;

                return scancode;
            }

            return 0;
        }
        case KBD_STATUS_PORT: {
            uint8_t status = KBD_STATUS_SYSTEM; // always set

            if (kbd->scanbuff_len > 0) {
                status |= KBD_STATUS_HAS_DATA;
            }

            // printf("reading kbd status: %02x\r\n", status);

            return status;
        }
        default: {
            return 0;
        }
    }
}

void
kbd_write_port(kbd_t* kbd, uint16_t port, uint8_t value)
{
    (void)kbd;

    switch (port) {
        case KBD_STATUS_PORT: {
            switch (value) {
                case 0xae: {
                    // enable first ps/2 port (keyboard)
                    // no-op for now
                    break;
                }
                default: {
                    printf("unknown keyboard command: %02x\r\n", value);
                    break;
                }
            }
            break;
        }
        default: {
            printf("unknown keyboard write: port %04x value %02x\r\n", port, value);
            break;
        }
    }
}

void
kbd_send_input(kbd_t* kbd, uint8_t scancode)
{
    if (kbd->scanbuff_len == KBD_SCANCODE_BUFFER_SIZE) {
        // buffer full, just drop input
        return;
    }

    kbd->scanbuff[kbd->scanbuff_len++] = scancode;
}
