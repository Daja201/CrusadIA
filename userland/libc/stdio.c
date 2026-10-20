#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static FILE g_stdin  = { 0, 0, 0 };
static FILE g_stdout = { 1, 0, 0 };
static FILE g_stderr = { 2, 0, 0 };

FILE* stdin  = &g_stdin;
FILE* stdout = &g_stdout;
FILE* stderr = &g_stderr;

static int mode_to_flags(const char* mode) {
    int plus = strchr(mode, '+') != NULL;
    if (mode[0] == 'w') return (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
    if (mode[0] == 'a') return (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
    return plus ? O_RDWR : O_RDONLY;
}

FILE* fopen(const char* path, const char* mode) {
    int fd = open(path, mode_to_flags(mode));
    if (fd < 0) return NULL;

    FILE* f = malloc(sizeof(FILE));
    if (!f) { close(fd); return NULL; }

    f->fd = fd;
    f->eof = 0;
    f->error = 0;
    return f;
}

int fclose(FILE* f) {
    if (!f || f == stdin || f == stdout || f == stderr) return 0;
    int r = close(f->fd);
    free(f);
    return r < 0 ? EOF : 0;
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* f) {
    if (!f || size == 0 || nmemb == 0) return 0;
    long total = (long)(size * nmemb);
    long got = read(f->fd, ptr, (size_t)total);
    if (got < 0) { f->error = 1; return 0; }
    if (got < total) f->eof = 1;
    return (size_t)got / size;
}

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* f) {
    if (!f || size == 0 || nmemb == 0) return 0;
    long total = (long)(size * nmemb);
    long put = write(f->fd, ptr, (size_t)total);
    if (put < 0) { f->error = 1; return 0; }
    return (size_t)put / size;
}

int fseek(FILE* f, long offset, int whence) {
    if (!f) return -1;
    long r = lseek(f->fd, offset, whence);
    if (r < 0) return -1;
    f->eof = 0;
    return 0;
}

long ftell(FILE* f) {
    if (!f) return -1;
    return lseek(f->fd, 0, SEEK_CUR);
}

int feof(FILE* f) { return f ? f->eof : 0; }
int ferror(FILE* f) { return f ? f->error : 0; }
int fflush(FILE* f) { (void)f; return 0; }

int fputc(int c, FILE* f) {
    unsigned char ch = (unsigned char)c;
    return fwrite(&ch, 1, 1, f) == 1 ? c : EOF;
}

int fputs(const char* s, FILE* f) {
    size_t len = strlen(s);
    return fwrite(s, 1, len, f) == len ? (int)len : EOF;
}

int putchar(int c) { return fputc(c, stdout); }
int puts(const char* s) {
    fputs(s, stdout);
    return fputc('\n', stdout);
}

int fgetc(FILE* f) {
    unsigned char c;
    if (fread(&c, 1, 1, f) != 1) return EOF;
    return c;
}

int getchar(void) { return fgetc(stdin); }

char* fgets(char* buf, int size, FILE* f) {
    if (size <= 0) return NULL;
    int i = 0;
    while (i < size - 1) {
        int c = fgetc(f);
        if (c == EOF) break;
        buf[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0 && feof(f)) return NULL;
    buf[i] = 0;
    return buf;
}

int vfprintf(FILE* f, const char* fmt, va_list args) {
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    fwrite(buf, 1, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1), f);
    return n;
}

int fprintf(FILE* f, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int r = vfprintf(f, fmt, args);
    va_end(args);
    return r;
}

int printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int r = vfprintf(stdout, fmt, args);
    va_end(args);
    return r;
}
