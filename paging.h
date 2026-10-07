#ifndef PAGING_H
#define PAGING_H
#include <stdint.h>
extern uint32_t* page_directory;
void map_page(uint32_t phys, uint32_t virt, uint32_t flags);
void map_page_dir(uint32_t* dir, uint32_t phys, uint32_t virt, uint32_t flags);
void unmap_page_dir(uint32_t* dir, uint32_t virt);
uint32_t* clone_kernel_page_directory(void);
void switch_page_directory(uint32_t* dir);
void init_paging(uint32_t fb_phys_addr, uint32_t fb_width, uint32_t fb_height, uint32_t fb_bpp);
#endif
