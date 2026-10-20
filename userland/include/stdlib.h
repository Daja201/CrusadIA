#ifndef USERLAND_STDLIB_H
#define USERLAND_STDLIB_H
#include <stddef.h>

void* malloc(size_t size);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);
void free(void* ptr);

int atoi(const char* s);
long atol(const char* s);
long strtol(const char* nptr, char** endptr, int base);

void exit(int code);
void abort(void);

char* getenv(const char* name);

int abs(int n);

void qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*));
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*));

#endif
