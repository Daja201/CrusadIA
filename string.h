#ifndef STRING_H
#define STRING_H
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
int strcmp(const char* a, const char* b);
char *strcpy(char *dest, const char *src);
char *strcat(char *dest, const char *src);
char *strrchr(const char* s, int c);
size_t strlen(const char* s);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
char* itoa(int value, char* buffer, int base);
int atoi(const char* s);
long strtol(const char* nptr, char** endptr, int base);
int strcasecmp(const char* a, const char* b);
char* strncpy(char* dest, const char* src, size_t n);
int memcmp(const void *a, const void *b, size_t n);
char* strdup(const char* s);
char* strchr(const char* s, int c);
char* strstr(const char* haystack, const char* needle);
int strncmp(const char* a, const char* b, size_t n);
void qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*));
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*));
int abs(int n);
void exit(int code);
void abort(void);
int vsnprintf(char* buf, size_t size, const char* fmt, va_list args);
int snprintf(char* buf, size_t size, const char* fmt, ...);
#define tolower(c) ((c) >= 'A' && (c) <= 'Z' ? (c) + 32 : (c))
#endif