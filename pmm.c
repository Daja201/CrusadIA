#include "pmm.h"
#include <string.h>

static uint32_t pmm_bitmap[PMM_MAX_BLOCKS / 32];
static uint32_t pmm_used_blocks = 0;

static inline void bitmap_set(int bit) {
    pmm_bitmap[bit / 32] |= (1 << (bit % 32));
}

static inline void bitmap_clear(int bit) {
    pmm_bitmap[bit / 32] &= ~(1 << (bit % 32));
}

static inline int bitmap_test(int bit) {
    return pmm_bitmap[bit / 32] & (1 << (bit % 32));
}

void pmm_init() {
    memset(pmm_bitmap, 0xFF, sizeof(pmm_bitmap));
    pmm_used_blocks = PMM_MAX_BLOCKS;
}

void pmm_init_region(uint32_t base_addr, uint32_t size) {
    uint32_t align = base_addr / PMM_BLOCK_SIZE;
    uint32_t blocks = size / PMM_BLOCK_SIZE;
    for (uint32_t i = 0; i < blocks; i++) {
        bitmap_clear(align + i);
        pmm_used_blocks--;
    }
    bitmap_set(0); 
}

void pmm_deinit_region(uint32_t base_addr, uint32_t size) {
    uint32_t align = base_addr / PMM_BLOCK_SIZE;
    uint32_t blocks = size / PMM_BLOCK_SIZE;

    for (uint32_t i = 0; i < blocks; i++) {
        bitmap_set(align + i);
        pmm_used_blocks++;
    }
}

uint32_t pmm_count_mem() {
    uint32_t free_blocks = PMM_MAX_BLOCKS - pmm_used_blocks;
    return free_blocks * 4; 
}

void* pmm_alloc_block() {
    for (uint32_t i = 0; i < (PMM_MAX_BLOCKS / 32); i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFF) { 
            for (int j = 0; j < 32; j++) { 
                int bit = i * 32 + j;
                if (!bitmap_test(bit)) {
                    bitmap_set(bit);
                    pmm_used_blocks++;
                    return (void*)(bit * PMM_BLOCK_SIZE);
                }
            }
        }
    }
    return 0;
}

void pmm_free_block(void* addr) {
    uint32_t block = (uint32_t)addr / PMM_BLOCK_SIZE;
    bitmap_clear(block);
    pmm_used_blocks--;
}
