#include <stdlib.h>
#include <stdio.h>

#include "panic.h"

__attribute__((noreturn))
void
fatal()
{
    while (1) {
        sleep(1);
    }
}

__attribute__((noreturn))
void
panic(const char* msg)
{
    printf("panic: %s\n", msg);
    fatal();
}
