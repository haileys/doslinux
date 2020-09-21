#include <stdint.h>
#include <stdio.h>
#include <sys/io.h>

#include "vga.h"

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25

struct vga_pos
vga_cursor_pos()
{
    uint16_t raw_pos = 0;
    outb(0x0f, 0x3d4);
    raw_pos |= inb(0x3d5);
    outb(0x0e, 0x3d4);
    raw_pos |= (uint16_t)inb(0x3d5) << 8;

    struct vga_pos pos;
    pos.x = raw_pos % SCREEN_WIDTH;
    pos.y = raw_pos / SCREEN_WIDTH;
    return pos;
}

void
vga_fix_cursor()
{
    struct vga_pos pos = vga_cursor_pos();
    printf("\033[%d;%dH", pos.y + 1, pos.x + 1);
    fflush(stdout);
}
