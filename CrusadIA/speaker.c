#include "speaker.h"
#include "io.h"

#define PIT_FREQ 1193180
#define PIT_CMD 0x43
#define PIT_CHANNEL2 0x42
#define SPEAKER_PORT 0x61

void speaker_play_tone(uint32_t frequency_hz, uint32_t ms) {
    if (frequency_hz == 0) return;
    uint16_t divisor = (uint16_t)(PIT_FREQ / frequency_hz);
    outb(PIT_CMD, 0xB6);
    outb(PIT_CHANNEL2, divisor & 0xFF);
    outb(PIT_CHANNEL2, (divisor >> 8) & 0xFF);
    uint8_t val = inb(SPEAKER_PORT);
    val |= 0x03;
    outb(SPEAKER_PORT, val);
    volatile unsigned int loops = 8000 * ms;
    for (volatile unsigned int i = 0; i < loops; i++) asm volatile ("nop");
    val = inb(SPEAKER_PORT);
    val &= ~0x03;
    outb(SPEAKER_PORT, val);
}
