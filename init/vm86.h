#ifndef VM86_H
#define VM86_H

#include <asm/vm86.h>
#include <stdbool.h>
#include <stdint.h>

#define FLAG_INTERRUPT              (1 << 9)
#define FLAG_VM8086                 (1 << 17)

typedef struct vm86_init {
    uint16_t ip;
    uint16_t cs;
    uint16_t flags;
    uint16_t sp;
    uint16_t ss;
} __attribute__((packed))
vm86_init_t;

__attribute__((noreturn)) void
vm86_run(vm86_init_t init_params);

#endif
