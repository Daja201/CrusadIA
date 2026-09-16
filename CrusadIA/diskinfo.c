#include <stdint.h>
#include "klog.h"
#include "fs.h"
#include "io.h"
extern superblock_t g_superblock;
void detect_disk() {
    uint8_t status = inb(0x1F7);
    if (status == 0xFF || status == 0x00) {
        kklog("No disk detected on primary ATA");
        return;
    }
    if (status & 0x01) {
        kklog("Disk error detected");
        return;
    }
    if (status & 0x40) {
        kklog("Disk ready (DRDY set)");
    } else {
        kklog("Disk not ready");
    }
}

void identify_disk() {
    uint16_t data[256];
    outb(0x1F6, 0xA0);
    outb(0x1F2, 0);
    outb(0x1F3, 0);
    outb(0x1F4, 0);
    outb(0x1F5, 0);
    outb(0x1F7, 0xEC);
    while (!(inb(0x1F7) & 0x08));
    for (int i = 0; i < 256; i++) {
        uint16_t val;
        asm volatile("inw %%dx, %0" : "=a"(val) : "d"(0x1F0));
        data[i] = val;
    }

    kklog("Disk IDENTIFY read OK");
    uint64_t sectors = ((uint64_t)data[100]) |
                       ((uint64_t)data[101] << 16) |
                       ((uint64_t)data[102] << 32) |
                       ((uint64_t)data[103] << 48);
    if (sectors == 0) {
        sectors = ((uint64_t)data[61] << 16) | (uint64_t)data[60];
    }

    if (sectors == 0) {
        kklog("size: unknown (IDENTIFY returned 0)");
        klog("FS superblock total_blocks:");
        kklogf("%llu", (unsigned long long)g_superblock.total_blocks);
    } else {
        char tmp[32]; int tp = 0;
        uint64_t tv = sectors;
        if (tv == 0) tmp[tp++] = '0';
        while (tv) { tmp[tp++] = '0' + (tv % 10); tv /= 10; }
        for (int i = 0; i < tp; ++i) tmp[i] = tmp[i]; 
        char buf[32]; for (int i = 0; i < tp; ++i) buf[i] = tmp[tp-1-i]; buf[tp] = '\0';
        klog("size (sectors):"); kklog(buf);
        uint64_t bytes = sectors * 512ULL;
        tp = 0; tv = bytes; if (tv == 0) tmp[tp++] = '0';
        while (tv) { tmp[tp++] = '0' + (tv % 10); tv /= 10; }
        for (int i = 0; i < tp; ++i) buf[i] = tmp[tp-1-i]; buf[tp] = '\0';
        klog("size (bytes):"); kklog(buf);   
        klog("FS superblock total_blocks:");
        kklogf("%llu", (unsigned long long)g_superblock.total_blocks);
        if (g_superblock.magic != 0x5A4C534A) kklog_color("drive is't properly formatted, use 'format' to format it", 0xFF0000);
        if (g_superblock.magic == 0x5A4C534A) kklog_color("drive formatted properly", 0x00FF00);
    }
    klog("\n");
}
