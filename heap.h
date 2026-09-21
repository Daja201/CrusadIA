#ifndef HEAP_H
#define HEAP_H
#include <stddef.h>
#include <stdint.h>

void heap_init(void);
void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);
void heap_stats(uint32_t* out_total, uint32_t* out_used, uint32_t* out_free);

#endif