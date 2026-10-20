#ifndef USERLAND_STDIO_H
#define USERLAND_STDIO_H
#include <stddef.h>
#include <stdarg.h>

#define EOF (-1)
#define BUFSIZ 512

typedef struct FILE {
    int fd;
    int eof;
    int error;
} FILE;

extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

FILE* fopen(const char* path, const char* mode);
int fclose(FILE* f);
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* f);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* f);
int fseek(FILE* f, long offset, int whence);
long ftell(FILE* f);
int feof(FILE* f);
int ferror(FILE* f);
int fflush(FILE* f);

int fputc(int c, FILE* f);
int fputs(const char* s, FILE* f);
int putchar(int c);
int puts(const char* s);
int fgetc(FILE* f);
int getchar(void);
char* fgets(char* buf, int size, FILE* f);

int printf(const char* fmt, ...);
int fprintf(FILE* f, const char* fmt, ...);
int sprintf(char* buf, const char* fmt, ...);
int snprintf(char* buf, size_t size, const char* fmt, ...);
int vsnprintf(char* buf, size_t size, const char* fmt, va_list args);
int vfprintf(FILE* f, const char* fmt, va_list args);

#endif
