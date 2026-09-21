#include "heap.h"
#include "pmm.h"
#include "string.h"

#define HEAP_MAGIC 0x5A4C534A
#define ALIGN 8
#define ALIGN_UP(x) (((x) + (ALIGN - 1)) & ~(size_t)(ALIGN - 1))

typedef struct block_header {
    uint32_t magic;
    size_t size;
    uint8_t free;
    struct block_header* next;
    struct block_header* prev;
} block_header_t;

#define HEADER_SIZE sizeof(block_header_t)

static block_header_t* heap_head = 0;
static block_header_t* heap_tail = 0;

void heap_init(void) {
    heap_head = 0;
    heap_tail = 0;
}

static block_header_t* grow_heap(size_t min_size) {
    size_t needed = HEADER_SIZE + min_size;
    size_t pages_needed = (needed + PMM_BLOCK_SIZE - 1) / PMM_BLOCK_SIZE;

    void* first = pmm_alloc_block();
    if (!first) return 0;

    uint32_t region_start = (uint32_t)first;
    uint32_t region_end = region_start + PMM_BLOCK_SIZE;
    size_t got_pages = 1;

    while (got_pages < pages_needed) {
        void* next_page = pmm_alloc_block();
        if (!next_page) break;
        if ((uint32_t)next_page == region_end) {
            region_end += PMM_BLOCK_SIZE;
            got_pages++;
        } else {
            pmm_free_block(next_page);
            break;
        }
    }

    block_header_t* block = (block_header_t*)region_start;
    block->magic = HEAP_MAGIC;
    block->size = (region_end - region_start) - HEADER_SIZE;
    block->free = 1;
    block->next = 0;
    block->prev = heap_tail;

    if (heap_tail) {
        heap_tail->next = block;
    } else {
        heap_head = block;
    }
    heap_tail = block;

    return block;
}

static void split_block(block_header_t* block, size_t size) {
    if (block->size < size + HEADER_SIZE + ALIGN) return;

    block_header_t* new_block = (block_header_t*)((uint8_t*)block + HEADER_SIZE + size);
    new_block->magic = HEAP_MAGIC;
    new_block->size = block->size - size - HEADER_SIZE;
    new_block->free = 1;
    new_block->next = block->next;
    new_block->prev = block;

    if (block->next) block->next->prev = new_block;
    else heap_tail = new_block;

    block->next = new_block;
    block->size = size;
}

static void coalesce(block_header_t* block) {
    if (block->next && block->next->free &&
        (uint8_t*)block + HEADER_SIZE + block->size == (uint8_t*)block->next) {
        block_header_t* dead = block->next;
        block->size += HEADER_SIZE + dead->size;
        block->next = dead->next;
        if (dead->next) dead->next->prev = block;
        else heap_tail = block;
    }

    if (block->prev && block->prev->free &&
        (uint8_t*)block->prev + HEADER_SIZE + block->prev->size == (uint8_t*)block) {
        block_header_t* prev = block->prev;
        prev->size += HEADER_SIZE + block->size;
        prev->next = block->next;
        if (block->next) block->next->prev = prev;
        else heap_tail = prev;
    }
}

void* malloc(size_t size) {
    if (size == 0) return 0;
    size = ALIGN_UP(size);

    block_header_t* cur = heap_head;
    while (cur) {
        if (cur->free && cur->size >= size) {
            split_block(cur, size);
            cur->free = 0;
            return (void*)((uint8_t*)cur + HEADER_SIZE);
        }
        cur = cur->next;
    }

    block_header_t* fresh = grow_heap(size);
    if (!fresh) return 0;
    if (fresh->size < size) return 0;

    split_block(fresh, size);
    fresh->free = 0;
    return (void*)((uint8_t*)fresh + HEADER_SIZE);
}

void free(void* ptr) {
    if (!ptr) return;
    block_header_t* block = (block_header_t*)((uint8_t*)ptr - HEADER_SIZE);
    if (block->magic != HEAP_MAGIC) return;
    if (block->free) return;
    block->free = 1;
    coalesce(block);
}

void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return 0;
    }

    block_header_t* block = (block_header_t*)((uint8_t*)ptr - HEADER_SIZE);
    if (block->magic != HEAP_MAGIC) return 0;

    size_t aligned = ALIGN_UP(size);
    if (block->size >= aligned) {
        split_block(block, aligned);
        return ptr;
    }

    void* new_ptr = malloc(size);
    if (!new_ptr) return 0;
    memcpy(new_ptr, ptr, block->size);
    free(ptr);
    return new_ptr;
}

void heap_stats(uint32_t* out_total, uint32_t* out_used, uint32_t* out_free) {
    uint32_t total = 0, used = 0, freeb = 0;
    block_header_t* cur = heap_head;
    while (cur) {
        total += (uint32_t)(cur->size + HEADER_SIZE);
        if (cur->free) freeb += (uint32_t)cur->size;
        else used += (uint32_t)cur->size;
        cur = cur->next;
    }
    if (out_total) *out_total = total;
    if (out_used) *out_used = used;
    if (out_free) *out_free = freeb;
}