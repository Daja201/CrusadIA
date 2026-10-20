#include "syscall.h"

unsigned int _syscall(unsigned int num, unsigned int a1, unsigned int a2, unsigned int a3) {
    unsigned int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2), "d"(a3) : "memory");
    return ret;
}
