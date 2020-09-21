#ifndef VGA_H
#define VGA_H

struct vga_pos {
    int x, y;
};

struct vga_pos
vga_cursor_pos();

void
vga_fix_cursor();

#endif
