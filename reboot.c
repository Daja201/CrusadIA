#include <stdint.h>
#include "reboot.h"
    struct idt_ptr {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed));

    static struct idt_ptr zero_idt = {
        .limit = 0,
        .base  = 0
    };

    __attribute__((noreturn))
    void reboot_triple_fault(void) {
        asm volatile (
            "cli\n"
            "lidt %0\n"
            "int $0x03\n"
            :
            : "m"(zero_idt)
        );

        for (;;);
    }