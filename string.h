#ifndef STRING_H
#define STRING_H
#include <stddef.h>
#include <stdint.h>
int strcmp(const char* a, const char* b);
size_t strlen(const char* s);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
char* itoa(int value, char* buffer, int base);
int atoi(const char* s);
long strtol(const char* nptr, char** endptr, int base);
int strcasecmp(const char* a, const char* b);
char* strncpy(char* dest, const char* src, size_t n);
int memcmp(const void *a, const void *b, size_t n);
#define tolower(c) ((c) >= 'A' && (c) <= 'Z' ? (c) + 32 : (c))
#endif
