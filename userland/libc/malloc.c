#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct block_hdr {
    size_t size;
    int free;
    struct block_hdr* next;
} block_hdr_t;

#define ALIGN(n) (((n) + 7u) & ~7u)
#define HDR_SIZE ALIGN(sizeof(block_hdr_t))

static block_hdr_t* g_head = NULL;

static block_hdr_t* grow(size_t size) {
    size_t total = HDR_SIZE + size;
    void* mem = sbrk((int)total);
    if (mem == (void*)-1) return NULL;

    block_hdr_t* blk = (block_hdr_t*)mem;
    blk->size = size;
    blk->free = 0;
    blk->next = NULL;
    return blk;
}

void* malloc(size_t size) {
    if (size == 0) return NULL;
    size = ALIGN(size);

    block_hdr_t* prev = NULL;
    block_hdr_t* cur = g_head;
    while (cur) {
        if (cur->free && cur->size >= size) {
            cur->free = 0;
            return (void*)((char*)cur + HDR_SIZE);
        }
        prev = cur;
        cur = cur->next;
    }

    block_hdr_t* blk = grow(size);
    if (!blk) return NULL;

    if (prev) prev->next = blk;
    else g_head = blk;

    return (void*)((char*)blk + HDR_SIZE);
}

void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void free(void* ptr) {
    if (!ptr) return;
    block_hdr_t* blk = (block_hdr_t*)((char*)ptr - HDR_SIZE);
    blk->free = 1;
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }

    block_hdr_t* blk = (block_hdr_t*)((char*)ptr - HDR_SIZE);
    if (blk->size >= size) return ptr;

    void* new_ptr = malloc(size);
    if (!new_ptr) return NULL;

    memcpy(new_ptr, ptr, blk->size);
    free(ptr);
    return new_ptr;
}
