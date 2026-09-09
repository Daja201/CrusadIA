#include <string.h>
#include "pmm.h" 
#include "vesa.h"

uint32_t next_free_page = 0x1000000;
uint32_t* page_directory = (uint32_t *)0x10000;

void map_page(uint32_t phys, uint32_t virt, uint32_t flags) {
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x03FF;
    if ((page_directory[pd_index] & 0x01) == 0) {
        uint32_t* new_table = (uint32_t*)next_free_page;
        next_free_page += 4096;
        memset(new_table, 0, 4096);
        page_directory[pd_index] = ((uint32_t)new_table) | 0x03;
    }
    uint32_t* page_table = (uint32_t*)(page_directory[pd_index] & ~0xFFF);
    page_table[pt_index] = (phys & ~0xFFF) | flags;
}

void init_paging(uint32_t fb_phys_addr, uint32_t fb_width, uint32_t fb_height, uint32_t fb_bpp) {
    page_directory = (uint32_t*)0x10000; 
    next_free_page = 0x1000000;
    memset(page_directory, 0, 4096);
    for (uint32_t i = 0; i < 1048576; i++) {
        uint32_t addr = i * 4096;
        map_page(addr, addr, 0x03);
    }
    uint32_t fb_size = fb_width * fb_height * (fb_bpp / 8);
    for (uint32_t i = 0; i < fb_size; i += 4096) {
        map_page(fb_phys_addr + i, fb_phys_addr + i, 0x03);
    }
    asm volatile("mov %0, %%cr3" :: "r"(page_directory));
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    asm volatile("mov %0, %%cr0" :: "r"(cr0));
}