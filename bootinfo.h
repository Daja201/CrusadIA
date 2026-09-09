#ifndef BOOTINFO_H
#define BOOTINFO_H
#include <stdint.h>
#include "klog.h"
extern int boot_has_fb;
extern uint32_t boot_fb_addr;
extern uint32_t boot_fb_width;
extern uint32_t boot_fb_height;
extern uint32_t boot_fb_bpp;
extern uint32_t boot_fb_pitch;
void parse_multiboot(uint32_t mb_magic, uint32_t mb_info);
#endif
