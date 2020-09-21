// #include "io.h"
// #include "kernel.h"
// #include "task.h"
// #include "debug.h"
// #include "framebuffer.h"

#include <bits/signal.h>
#include <bits/syscall.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <signal.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include "vm86.h"
#include "panic.h"
#include "io.h"

typedef union reg32 {
    uint32_t dword;
    struct {
        uint16_t lo;
        uint16_t hi;
    } word;
    struct {
        uint8_t lo;
        uint8_t hi;
        uint8_t res1;
        uint8_t res2;
    } byte;
}
reg32_t;

// this is like vm86_regs, but all registers are of union type reg32_t
typedef struct regs {
    reg32_t ebx;
    reg32_t ecx;
    reg32_t edx;
    reg32_t esi;
    reg32_t edi;
    reg32_t ebp;
    reg32_t eax;
    reg32_t __null_ds;
    reg32_t __null_es;
    reg32_t __null_fs;
    reg32_t __null_gs;
    reg32_t orig_eax;
    reg32_t eip;
    reg32_t cs;
    reg32_t eflags;
    reg32_t esp;
    reg32_t ss;

    reg32_t es16;
    reg32_t ds16;
    reg32_t fs16;
    reg32_t gs16;
}
regs_t;

typedef struct task {
    regs_t* regs;
    bool interrupts_enabled;
    bool pending_interrupt;
    uint8_t pending_interrupt_nr;
}
task_t;

enum rep_kind {
    NONE,
    REP,
};

enum bit_size {
    BITS16 = 0,
    BITS32 = 1,
};

static void
print(const char* s)
{
    printf("%s", s);
}

static void
print8(uint8_t u)
{
    printf("%02x", u);
}

static void
print16(uint16_t u)
{
    printf("%04x", u);
}

static void
print32(uint32_t u)
{
    printf("%08x", u);
}

static void*
linear(uint16_t segment, uint16_t offset)
{
    uint32_t seg32 = segment;
    uint32_t off32 = offset;
    uint32_t lin = (seg32 << 4) + off32;
    return (void*)lin;
}

static uint8_t
peek8(uint16_t segment, uint16_t offset)
{
    return *(uint8_t*)linear(segment, offset);
}

static void
poke8(uint16_t segment, uint16_t offset, uint8_t value)
{
    *(uint8_t*)linear(segment, offset) = value;
}

static uint16_t
peek16(uint16_t segment, uint16_t offset)
{
    return *(uint16_t*)linear(segment, offset);
}

static void
poke16(uint16_t segment, uint16_t offset, uint16_t value)
{
    *(uint16_t*)linear(segment, offset) = value;
}

static void
poke32(uint16_t segment, uint16_t offset, uint32_t value)
{
    *(uint32_t*)linear(segment, offset) = value;
}

static uint8_t
peekip(regs_t* regs, uint16_t offset)
{
    return peek8(regs->cs.word.lo, regs->eip.word.lo + offset);
}

struct ivt_descr {
    uint16_t offset;
    uint16_t segment;
};

static struct ivt_descr* const IVT = 0;

static void
push16(regs_t* regs, uint16_t value)
{
    regs->esp.word.lo -= 2;
    poke16(regs->ss.word.lo, regs->esp.word.lo, value);
}

static uint16_t
pop16(regs_t* regs)
{
    uint16_t value = peek16(regs->ss.word.lo, regs->esp.word.lo);
    regs->esp.word.lo += 2;
    return value;
}

static void
do_pushf(task_t* task)
{
    uint16_t flags = task->regs->eflags.word.lo;
    if (task->interrupts_enabled) {
        flags |= FLAG_INTERRUPT;
    } else {
        flags &= ~FLAG_INTERRUPT;
    }
    push16(task->regs, flags);
}

static void do_pending_int(task_t* task);

static void
do_popf(task_t* task)
{
    uint16_t flags = pop16(task->regs);
    // copy IF flag to variable
    if (flags & FLAG_INTERRUPT) {
        task->interrupts_enabled = true;
    } else {
        task->interrupts_enabled = false;
    }
    task->regs->eflags.word.lo = flags;
    // force interrupts on in real eflags
    task->regs->eflags.word.lo |= FLAG_INTERRUPT;

    if (task->interrupts_enabled) {
        do_pending_int(task);
    }
}

static void
do_int(task_t* task, uint8_t vector)
{
    do_pushf(task);
    push16(task->regs, task->regs->cs.word.lo);
    push16(task->regs, task->regs->eip.word.lo);
    struct ivt_descr* descr = &IVT[vector];
    // printf("  handler for softint at %04x:%04x\n", descr->segment, descr->offset);
    task->regs->cs.word.lo = descr->segment;
    task->regs->eip.dword = descr->offset;
}

static void
do_software_int(task_t* task, uint8_t vector)
{
    // TODO - doslinux edit
    // if (!task->has_reset && vector == 0x7f) {
    //     // guest issued reset syscall
    //     // we're done with our real mode initialisation
    //     task->has_reset = true;

    //     print("SYSCALL: reset\n");
    //     lomem_reset();
    //     framebuffer_reset();
    //     return;
    // }

    do_int(task, vector);
}

static void
do_pending_int(task_t* task)
{
    if (task->pending_interrupt) {
        task->pending_interrupt = false;
        do_int(task, task->pending_interrupt_nr);
    }
}

static void
do_iret(task_t* task)
{
    task->regs->eip.dword = pop16(task->regs);
    task->regs->cs.word.lo = pop16(task->regs);
    do_popf(task);
}

static uint8_t
do_inb(uint16_t port)
{
    uint8_t value = inb(port);
    print("inb port ");
    print16(port);
    print(" => ");
    print8(value);
    print("\n");
    return value;
}

static uint16_t
do_inw(uint16_t port)
{
    uint16_t value = inw(port);
    print("inw port ");
    print16(port);
    print(" => ");
    print16(value);
    print("\n");
    return value;
}

static uint32_t
do_ind(uint16_t port)
{
    uint32_t value = ind(port);
    print("ind port ");
    print16(port);
    print(" => ");
    print32(value);
    print("\n");
    return value;
}

static void
do_outb(task_t* task, uint16_t port, uint8_t value)
{
    if (port != 0x20 || value != 0x20) {
        print("outb port ");
        print16(port);
        print(" <= ");
        print8(value);
        print("\n");
    }

    // TODO - doslinux edit
    // if (task->has_reset) {
    //     if (IO_VGA_LO <= port && port <= IO_VGA_HI) {
    //         framebuffer_outb(port, value);
    //         return;
    //     }
    // }

    outb(port, value);
}

static void
do_outw(task_t* task, uint16_t port, uint16_t value)
{
    print("outw port ");
    print16(port);
    print(" <= ");
    print16(value);
    print("\n");

    // TODO - doslinux edit
    // if (task->has_reset) {
    //     if (IO_VGA_LO <= port && port <= IO_VGA_HI) {
    //         // TODO do we ever outw to the VGA?
    //         framebuffer_outb(port, value & 0xff);
    //         return;
    //     }
    // }

    outw(port, value);
}

static void
do_outd(task_t* task, uint16_t port, uint32_t value)
{
    print("outd port ");
    print16(port);
    print(" <= ");
    print32(value);
    print("\n");

    // TODO - doslinux edit
    // if (task->has_reset) {
    //     if (IO_VGA_LO <= port && port <= IO_VGA_HI) {
    //         // TODO do we ever outd to the VGA?
    //         framebuffer_outb(port, value & 0xff);
    //         return;
    //     }
    // }

    outd(port, value);
}

static void
do_insb(regs_t* regs)
{
    poke8(regs->es16.word.lo, regs->edi.word.lo, do_inb(regs->edx.word.lo));
    regs->edi.word.lo += 1;
}

static void
do_insw(regs_t* regs)
{
    uint16_t value = do_inw(regs->edx.word.lo);
    poke16(regs->es16.word.lo, regs->edi.word.lo, value);
    regs->edi.word.lo += 2;
}

static void
do_insd(regs_t* regs)
{
    uint32_t value = do_ind(regs->edx.word.lo);
    poke32(regs->es16.word.lo, regs->edi.word.lo, value);
    regs->edi.word.lo += 4;
}

static uint32_t
rep_count(regs_t* regs, enum rep_kind rep_kind, enum bit_size operand, enum bit_size address)
{
    if (rep_kind == NONE) {
        return 1;
    } else {
        if (operand ^ address) {
            return regs->ecx.dword;
        } else {
            return regs->ecx.word.lo;
        }
    }
}

static void
emulate_insn(task_t* task)
{
    enum bit_size address = BITS16;
    enum bit_size operand = BITS16;
    enum rep_kind rep_kind = NONE;

prefix:
    switch (peekip(task->regs, 0)) {
        case 0x66:
            operand = BITS32;
            task->regs->eip.word.lo++;
            goto prefix;
        case 0xf3:
            rep_kind = REP;
            task->regs->eip.word.lo++;
            goto prefix;
        default:
            break;
    }

#define REPEAT(blk) do { \
        for (uint32_t count = rep_count(task->regs, rep_kind, operand, address); count; count--) { \
            blk \
        } \
        if (rep_kind != NONE) { \
            task->regs->ecx.dword = 0; \
        } \
    } while (0)

    switch (peekip(task->regs, 0)) {
    case 0x66:
        // o32 prefix
        panic("O32 prefix in GPF'd instruction");
    case 0x6c: {
        // INSB
        print("  INSB\n");
        REPEAT({
            do_insb(task->regs);
        });
        task->regs->eip.word.lo += 1;
        return;
    }
    case 0x6d: {
        // INSW
        print("  INSW\n");

        REPEAT({
            if (operand == BITS32) {
                do_insd(task->regs);
            } else {
                do_insw(task->regs);
            }
        });

        task->regs->eip.word.lo += 1;
        return;
    }
    case 0x9c: {
        print("  PUSHF\n");
        // PUSHF
        do_pushf(task);
        task->regs->eip.word.lo += 1;
        return;
    }
    case 0x9d: {
        print("  POPF\n");
        // POPF
        do_popf(task);
        task->regs->eip.word.lo += 1;
        return;
    }
    case 0xcd: {
        // INT imm
        print("  INT\n");
        uint16_t vector = peekip(task->regs, 1);
        task->regs->eip.word.lo += 2;
        do_software_int(task, vector);
        return;
    }
    case 0xcf:
        // IRET
        print("  IRET\n");
        do_iret(task);
        return;
    case 0xe4:
        // INB imm
        print("  INB imm\n");
        task->regs->eax.byte.lo = do_inb(peekip(task->regs, 1));
        task->regs->eip.word.lo += 2;
        return;
    case 0xe5:
        // INW imm
        print("  INW imm\n");
        if (operand == BITS32) {
            task->regs->eax.dword = do_ind(peekip(task->regs, 1));
        } else {
            task->regs->eax.word.lo = do_inw(peekip(task->regs, 1));
        }
        task->regs->eip.word.lo += 2;
        return;
    case 0xe6:
        // OUTB imm
        print("  OUTB imm\n");
        do_outb(task, peekip(task->regs, 1), task->regs->eax.byte.lo);
        task->regs->eip.word.lo += 2;
        return;
    case 0xe7:
        // OUTW imm
        print("  OUTW imm\n");
        if (operand == BITS32) {
            do_outd(task, peekip(task->regs, 1), task->regs->eax.dword);
        } else {
            do_outw(task, peekip(task->regs, 1), task->regs->eax.word.lo);
        }
        task->regs->eip.word.lo += 2;
        return;
    case 0xec:
        // INB DX
        print("  INB DX\n");
        task->regs->eax.byte.lo = do_inb(task->regs->edx.word.lo);
        task->regs->eip.word.lo += 1;
        return;
    case 0xed:
        // INW DX
        print("  INW DX\n");
        if (operand == BITS32) {
            task->regs->eax.dword = do_ind(task->regs->edx.word.lo);
        } else {
            task->regs->eax.word.lo = do_inw(task->regs->edx.word.lo);
        }
        task->regs->eip.word.lo += 1;
        return;
    case 0xee:
        // OUTB DX
        print("  OUTB DX\n");
        do_outb(task, task->regs->edx.word.lo, task->regs->eax.byte.lo);
        task->regs->eip.word.lo += 1;
        return;
    case 0xef:
        // OUTW DX
        print("  OUTW DX\n");
        if (operand == BITS32) {
            do_outd(task, task->regs->edx.word.lo, task->regs->eax.dword);
        } else {
            do_outw(task, task->regs->edx.word.lo, task->regs->eax.word.lo);
        }
        task->regs->eip.word.lo += 1;
        return;
    case 0xf4: {
        // HLT
        print("  HLT\n");
        if (!task->interrupts_enabled) {
            panic("8086 task halted CPU with interrupts disabled");
        }
        task->regs->eip.word.lo += 1;
        return;
    }
    case 0xfa:
        // CLI
        print("  CLI\n");
        task->interrupts_enabled = false;
        task->regs->eip.word.lo += 1;
        return;
    case 0xfb:
        // STI
        print("  STI\n");
        task->interrupts_enabled = true;
        task->regs->eip.word.lo += 1;
        do_pending_int(task);
        return;
    default:
        printf("[%04x:%04x] unknown instruction in gpf: %02x\n",
            task->regs->cs.word.lo,
            task->regs->eip.word.lo,
            peekip(task->regs, 0));
        fatal();
    }

    panic("unhandled GPF");
}

void
vm86_interrupt(task_t* task, uint8_t vector)
{
    if (task->interrupts_enabled) {
        printf("Dispatching interrupt %04x\n", vector);
        do_int(task, vector);
    } else {
        printf("Setting pending interrupt %04x\n", vector);
        task->pending_interrupt = true;
        task->pending_interrupt_nr = vector;
    }
}

void
vm86_gpf(task_t* task)
{
    emulate_insn(task);

    // FIXME something is setting NT, IOPL=3, and a reserved bit in EFLAGS
    // not sure what's happening, but this causes things to break and clearing
    // these bits seems to work around it for now ¯\_(ツ)_/¯
    task->regs->eflags.dword &= ~(0xf << 12);
}

static volatile sig_atomic_t
received_irq = 0;

static void
on_sigio()
{
    received_irq = 1;
    write(0, "SIGIO\n", 6);
}

static void
setup_sigio()
{
    struct sigaction sa = { 0 };
    sa.sa_handler = on_sigio;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGIO, &sa, NULL)) {
        perror("sigaction SIGIO");
        fatal();
    }
}

static void
setup_stdin()
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

    if (fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK)) {
        perror("set stdin nonblock");
        fatal();
    }

    // put stdin into raw mode
    struct termios attr;
    if (tcgetattr(STDIN_FILENO, &attr)) {
        perror("tcgetattr");
        fatal();
    }

    // put terminal into raw mode
    // see https://linux.die.net/man/3/tcgetattr
    attr.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                        | INLCR | IGNCR | ICRNL | IXON);
    attr.c_oflag &= ~OPOST;
    attr.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    attr.c_cflag &= ~(CSIZE | PARENB);
    attr.c_cflag |= CS8;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &attr)) {
        perror("tcsetattr");
        fatal();
    }
}

__attribute__((noreturn)) void
vm86_run(struct vm86_init init_params)
{
    struct vm86plus_struct vm86 = { 0 };
    vm86.regs.cs = init_params.cs;
    vm86.regs.eip = init_params.ip;
    vm86.regs.eflags = init_params.flags;
    vm86.regs.esp = init_params.sp;
    vm86.regs.ss = init_params.ss;
    vm86.regs.ds = init_params.ss;
    vm86.regs.es = init_params.ss;
    vm86.regs.fs = init_params.ss;
    vm86.cpu_type = 3;

    task_t task = { 0 };
    task.regs = (void*)&vm86.regs;

    setup_sigio();
    setup_stdin();

    // if (syscall(SYS_vm86, VM86_REQUEST_IRQ, (SIGIO << 8) | 0x16)) {
    //     perror("request IRQ 0x16");
    //     fatal();
    // }

    while (1) {
        int rc = syscall(SYS_vm86, VM86_ENTER, &vm86);

        switch (VM86_TYPE(rc)) {
            case VM86_SIGNAL: {
                if (received_irq) {
                    printf("RECEIVED IRQ!\n");
                }
                break;
            }
            case VM86_UNKNOWN: {
                printf("VM86 GPF\n");
                vm86_gpf(&task);
                break;
            }
            case VM86_INTx: {
                // printf("VM86_INTx: %02x (at %04x:%04x)\n",
                //     VM86_ARG(rc), task.regs->cs.word.lo, task.regs->eip.word.lo);

                // printf("  bytes around site:\n   ");
                // for (int i = -8; i < 9; i++) {
                //     if (i == 0) {
                //         printf(" ->");
                //     }
                //     printf(" %02x", ((uint8_t*)linear(task.regs->cs.word.lo, task.regs->eip.word.lo))[i]);
                // }
                // printf("\n");

                do_software_int(&task, VM86_ARG(rc));

                // printf("  bytes around handler:\n   ");
                // for (int i = -8; i < 9; i++) {
                //     if (i == 0) {
                //         printf(" ->");
                //     }
                //     printf(" %02x", ((uint8_t*)linear(task.regs->cs.word.lo, task.regs->eip.word.lo))[i]);
                // }
                // printf("\n");

                break;
            }
            case VM86_STI: {
                printf("VM86_STI\n");
                break;
            }
            case VM86_PICRETURN: {
                printf("VM86_PICRETURN\n");
                break;
            }
            case VM86_TRAP: {
                printf("VM86_TRAP\n");
                break;
            }
            default: {
                printf("unknown vm86 return code: %d\n", rc);
                break;
            }
        }
    }
}
