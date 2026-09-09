#ifndef PMM_H
#define PMM_H
#include <stdint.h>
#define PMM_BLOCK_SIZE 4096
#define PMM_MAX_BLOCKS 1048576
uint32_t pmm_count_block(void);
uint32_t pmm_count_mem(void);
void pmm_init(void);
void pmm_init_region(uint32_t base_addr, uint32_t size);
void pmm_deinit_region(uint32_t base_addr, uint32_t size);
void* pmm_alloc_block(void);
void pmm_free_block(void* addr);
#endif