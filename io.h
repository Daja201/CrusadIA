#ifndef IO_H
#define IO_H
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a" (val), "Nd" (port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a" (val), "Nd" (port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ("inw %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val) {
    asm volatile ("outl %0, %1" : : "a" (val), "Nd" (port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}

static inline void insw(uint16_t port, void *addr, uint32_t count) {
    asm volatile ("cld; rep insw" :
                  "+D" (addr), "+c" (count) :
                  "d" (port) : "memory");
}

static inline void outsw(uint16_t port, const void *addr, uint32_t count) {
    asm volatile ("cld; rep outsw" :
                  "+S" (addr), "+c" (count) :
                  "d" (port) : "memory");
}

static inline void pit_delay_us(uint32_t us) {
    while (us > 0) {
        uint32_t chunk = us > 50000 ? 50000 : us;
        uint32_t count = (uint32_t)(((uint64_t)chunk * 1193182ull) / 1000000ull);
        if (count < 1) count = 1;
        if (count > 0xFFFF) count = 0xFFFF;
        outb(0x61, (inb(0x61) & 0xFC) | 0x01);
        outb(0x43, 0xB0);
        outb(0x42, (uint8_t)(count & 0xFF));
        outb(0x42, (uint8_t)((count >> 8) & 0xFF));
        while (!(inb(0x61) & 0x20)) {
            asm volatile("pause");
        }
        us -= chunk;
    }
}

static inline void pit_delay_ms(uint32_t ms) {
    pit_delay_us(ms * 1000u);
}

#endif
