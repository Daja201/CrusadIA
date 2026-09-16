#include "rtc.h"
#include <stdint.h>
#include "commands.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static uint8_t rtc_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static int rtc_updating() {
    outb(0x70, 0x0A);
    return inb(0x71) & 0x80;
}

static int bcd_to_bin(int val) {
    return (val & 0x0F) + ((val >> 4) * 10);
}

void rtc_get_datetime(
    int* year,
    int* month,
    int* day,
    int* hour,
    int* min,
    int* sec
)

{
    uint8_t statusB;
    while (rtc_updating());
    *sec   = rtc_read(0x00);
    *min   = rtc_read(0x02);
    *hour  = rtc_read(0x04) + timezone;
    *day   = rtc_read(0x07);
    *month = rtc_read(0x08);
    *year  = rtc_read(0x09);
    statusB = rtc_read(0x0B);
    if (!(statusB & 0x04)) {
        *sec   = bcd_to_bin(*sec);
        *min   = bcd_to_bin(*min);
        *hour  = bcd_to_bin(*hour & 0x7F);
        *day   = bcd_to_bin(*day);
        *month = bcd_to_bin(*month);
        *year  = bcd_to_bin(*year);
    }
    if (!(statusB & 0x02) && (*hour & 0x80)) {
        *hour = ((*hour & 0x7F) + 12) % 24;
    }
    if (*hour >=24) {
        *hour -= 24;
    }
    *year += 2000;
}
