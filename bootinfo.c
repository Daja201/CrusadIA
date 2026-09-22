#include "bootinfo.h"
#include <stdint.h>
#include "pmm.h"
int boot_has_fb = 0;
uint32_t boot_fb_addr = 0;
uint32_t boot_fb_width = 0;
uint32_t boot_fb_height = 0;
uint32_t boot_fb_bpp = 0;
uint32_t boot_fb_pitch = 0;

void parse_multiboot(uint32_t mb_magic, uint32_t mb_info) {
    if (mb_magic != 0x2BADB002) {
        klog("VBE FAIL: Invalid Multiboot Magic Number!");
        return;
    }
    if (mb_info == 0) {
        klog("VBE FAIL: mb_info pointer is 0!");
        return;
    }

    uint32_t *mb = (uint32_t*) (uintptr_t) mb_info;
    uint32_t flags = mb[0];
    uint8_t *mb_bytes = (uint8_t*)mb;

    pmm_init();

    if (flags & (1 << 6)) {
        uint32_t mmap_length = *(uint32_t*)(mb_bytes + 44);
        uint32_t mmap_addr   = *(uint32_t*)(mb_bytes + 48);

        klogf("Memory map detected: addr=0x%x, len=%d\n", mmap_addr, mmap_length);

        uint32_t current_addr = mmap_addr;
        while (current_addr < mmap_addr + mmap_length) {
            uint32_t size = *(uint32_t*)current_addr;
            uint32_t base_low = *(uint32_t*)(current_addr + 4);
            uint32_t len_low  = *(uint32_t*)(current_addr + 12);
            uint32_t type     = *(uint32_t*)(current_addr + 20);

            if (type == 1) {
                klogf("  Free RAM: base=0x%x, size=%d bytes\n", base_low, len_low);
                pmm_init_region(base_low, len_low);
            } else {
                klogf("  Reserved: base=0x%x, size=%d bytes\n", base_low, len_low);
            }
            
            current_addr += size + 4;
        }
        pmm_deinit_region(0x0, 0x100000);
        pmm_deinit_region(0x100000, 0x500000);   /* kernel + bss (grew with TinyCC): 1MB..6MB */
    } else {
        klog("WARNING: No memory map provided by GRUB!\n");
    }
    
    if (flags & (1 << 12)) {
        uint32_t fb_addr_lo = *(uint32_t*)(mb_bytes + 88);
        uint32_t pitch      = *(uint32_t*)(mb_bytes + 96);
        uint32_t width      = *(uint32_t*)(mb_bytes + 100);
        uint32_t height     = *(uint32_t*)(mb_bytes + 104);
        uint8_t  bpp        = *(uint8_t*)(mb_bytes + 108);

        if (fb_addr_lo != 0) {
            boot_has_fb = 1;
            boot_fb_addr = fb_addr_lo;
            boot_fb_width = width;
            boot_fb_height = height;
            boot_fb_bpp = bpp;
            boot_fb_pitch = pitch;
            return;
        }
    }

    if (flags & (1 << 11)) {
        uint32_t vbe_mode_info_ptr = mb[19];
        if (vbe_mode_info_ptr != 0) {
            uint8_t *mode = (uint8_t*)(uintptr_t)vbe_mode_info_ptr;
            uint16_t x = *(uint16_t*)(mode + 18);
            uint16_t y = *(uint16_t*)(mode + 20);
            uint8_t bpp = *(uint8_t*)(mode + 25);
            uint32_t phys = *(uint32_t*)(mode + 40);

            if (phys != 0) {
                boot_has_fb = 1;
                boot_fb_addr = phys;
                boot_fb_width = x;
                boot_fb_height = y;
                boot_fb_bpp = bpp;
                boot_fb_pitch = (bpp >= 8) ? (x * ((bpp + 7) / 8)) : x;
                return;
            }
        }
    }

    klog("NO FRAMEBUFFER");
}