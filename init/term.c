#include <bits/signal.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/io.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "panic.h"
#include "term.h"

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25

static struct termios normal_term;
static struct termios raw_term;

static int
vga_cursor_line()
{
    uint16_t raw_pos = 0;
    outb(0x0f, 0x3d4);
    raw_pos |= inb(0x3d5);
    outb(0x0e, 0x3d4);
    raw_pos |= (uint16_t)inb(0x3d5) << 8;

    return raw_pos / SCREEN_WIDTH;
}

void
term_init()
{
    if (tcgetattr(STDIN_FILENO, &normal_term)) {
        perror("tcgetattr");
        fatal();
    }

    raw_term = normal_term;

    // see https://linux.die.net/man/3/tcgetattr for these flags
    raw_term.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                           | INLCR | IGNCR | ICRNL | IXON);
    raw_term.c_oflag &= ~OPOST;
    raw_term.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw_term.c_cflag &= ~(CSIZE | PARENB);
    raw_term.c_cflag |= CS8;
}

void
term_yield_to_dos()
{
    // get raw scancodes from stdin rather than keycodes or ascii

    if (ioctl(STDIN_FILENO, KDSKBMODE, K_RAW)) {
        perror("set stdin raw mode");
        fatal();
    }

    // arrange for SIGIO to be raised when input is available

    if (fcntl(STDIN_FILENO, F_SETSIG, SIGIO)) {
        perror("set stdin async signal");
        fatal();
    }

    if (fcntl(STDIN_FILENO, F_SETOWN, getpid())) {
        perror("set stdin owner");
        fatal();
    }

    if (fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK | O_ASYNC)) {
        perror("set stdin nonblock");
        fatal();
    }

    // put stdin into raw mode

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_term)) {
        perror("tcsetattr");
        fatal();
    }
}

void
term_acquire()
{
    // select translated keyboard mode

    if (ioctl(STDIN_FILENO, KDSKBMODE, K_XLATE)) {
        perror("set stdin xlate mode");
        fatal();
    }

    // disable O_NONBLOCK and O_ASYNC on terminal

    if (fcntl(STDIN_FILENO, F_SETFL, 0)) {
        perror("set stdin normal");
        fatal();
    }

    // put stdin into normal mode

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &normal_term)) {
        perror("tcsetattr");
        fatal();
    }

    // replicate VGA cursor position in console

    int line = vga_cursor_line();
    printf("\033[%d;%dH", line + 1, 1);
    fflush(stdout);
}
