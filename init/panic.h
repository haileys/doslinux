#ifndef PANIC_H
#define PANIC_H

__attribute__((noreturn))
void
fatal();

__attribute__((noreturn))
void
panic(const char* msg);

#endif
