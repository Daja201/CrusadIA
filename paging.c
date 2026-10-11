#include "string.h"
#include "pmm.h"
#include "vesa.h"
#include "paging.h"
#include "klog.h"

uint32_t* page_directory = (uint32_t *)0x10000;

static void protect_table_frame(uint32_t phys) {
    uint32_t pd_index = phys >> 22;
    uint32_t pt_index = (phys >> 12) & 0x03FF;
    if ((page_directory[pd_index] & 0x01) == 0) {
        void* t = pmm_alloc_block();
        if (!t) return;
        memset(t, 0, 4096);
        page_directory[pd_index] = ((uint32_t)t) | 0x07;
    }
    uint32_t* table = (uint32_t*)(page_directory[pd_index] & ~0xFFFu);
    table[pt_index] = (phys & ~0xFFFu) | 0x03;
}

void map_page_dir(uint32_t* dir, uint32_t phys, uint32_t virt, uint32_t flags) {
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x03FF;
    if ((dir[pd_index] & 0x01) == 0) {
        void* new_table = pmm_alloc_block();
        if (!new_table) {
            klog_status("PAGING: OUT OF MEMORY FOR PAGE TABLE", 0xFF0000);
            return;
        }
        memset(new_table, 0, 4096);
        protect_table_frame((uint32_t)new_table);
        dir[pd_index] = ((uint32_t)new_table) | (flags & 0x07) | 0x01;
    }
    uint32_t* page_table = (uint32_t*)(dir[pd_index] & ~0xFFF);
    page_table[pt_index] = (phys & ~0xFFF) | flags;
}

void map_page(uint32_t phys, uint32_t virt, uint32_t flags) {
    map_page_dir(page_directory, phys, virt, flags);
}

void unmap_page_dir(uint32_t* dir, uint32_t virt) {
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x03FF;
    if ((dir[pd_index] & 0x01) == 0) {
        return;
    }
    uint32_t* old_table = (uint32_t*)(dir[pd_index] & ~0xFFF);
    void* new_table = pmm_alloc_block();
    if (!new_table) {
        klog_status("PAGING: OUT OF MEMORY FOR PAGE TABLE", 0xFF0000);
        return;
    }
    protect_table_frame((uint32_t)new_table);
    uint32_t* nt = (uint32_t*)new_table;
    for (int i = 0; i < 1024; i++) {
        nt[i] = old_table[i];
    }
    nt[pt_index] = 0;
    dir[pd_index] = ((uint32_t)new_table) | (dir[pd_index] & 0xFFF);
}

uint32_t* clone_kernel_page_directory(void) {
    void* new_dir = pmm_alloc_block();
    if (!new_dir) {
        klog_status("PAGING: OUT OF MEMORY FOR PAGE DIRECTORY", 0xFF0000);
        return 0;
    }
    protect_table_frame((uint32_t)new_dir);
    uint32_t* nd = (uint32_t*)new_dir;
    for (int i = 0; i < 1024; i++) {
        nd[i] = page_directory[i];
    }
    return nd;
}

void switch_page_directory(uint32_t* dir) {
    asm volatile("mov %0, %%cr3" :: "r"(dir));
}

void init_paging(uint32_t fb_phys_addr, uint32_t fb_width, uint32_t fb_height, uint32_t fb_bpp) {
    page_directory = (uint32_t*)0x10000;
    memset(page_directory, 0, 4096);
    for (uint32_t i = 0; i < 1048576; i++) {
        uint32_t addr = i * 4096;
        map_page(addr, addr, 0x07);
    }
    uint32_t fb_size = fb_width * fb_height * (fb_bpp / 8);
    for (uint32_t i = 0; i < fb_size; i += 4096) {
        map_page(fb_phys_addr + i, fb_phys_addr + i, 0x07);
    }

    map_page(0, 0, 0x02);
    map_page(0x10000, 0x10000, 0x03);

    asm volatile("mov %0, %%cr3" :: "r"(page_directory));
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    asm volatile("mov %0, %%cr0" :: "r"(cr0));
}