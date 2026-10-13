#ifndef HEAP_H
#define HEAP_H
#include <stddef.h>
#include <stdint.h>

typedef struct heap_block_header heap_block_header_t;

typedef struct {
    heap_block_header_t* head;
    heap_block_header_t* tail;
} heap_arena_t;

void heap_init(void);
void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);
void heap_stats(uint32_t* out_total, uint32_t* out_used, uint32_t* out_free);

void heap_arena_init(heap_arena_t* arena);
void* heap_arena_alloc(heap_arena_t* arena, size_t size, int owner_slot);
void heap_arena_free(heap_arena_t* arena, void* ptr);
void* heap_arena_calloc(heap_arena_t* arena, size_t nmemb, size_t size, int owner_slot);
void* heap_arena_realloc(heap_arena_t* arena, void* ptr, size_t size, int owner_slot);

#endif