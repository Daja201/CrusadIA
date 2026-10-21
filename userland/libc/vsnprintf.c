#include <stdio.h>
#include <string.h>

typedef struct {
    char* buf;
    size_t size;
    size_t pos;
} sink_t;

static void emit(sink_t* s, char c) {
    if (s->pos + 1 < s->size) s->buf[s->pos] = c;
    s->pos++;
}

static void emit_str(sink_t* s, const char* str) {
    while (*str) emit(s, *str++);
}

static void emit_uint(sink_t* s, unsigned long v, int base, int upper) {
    char tmp[32];
    int n = 0;
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    if (v == 0) { emit(s, '0'); return; }

    while (v) {
        tmp[n++] = digits[v % base];
        v /= base;
    }
    while (n > 0) emit(s, tmp[--n]);
}

static void emit_int(sink_t* s, long v) {
    if (v < 0) {
        emit(s, '-');
        emit_uint(s, (unsigned long)(-v), 10, 0);
    } else {
        emit_uint(s, (unsigned long)v, 10, 0);
    }
}

static void emit_uint64(sink_t* s, unsigned long long v, int base, int upper) {
    char tmp[64];
    int n = 0;
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    if (v == 0) { emit(s, '0'); return; }

    while (v) {
        tmp[n++] = digits[v % (unsigned)base];
        v /= (unsigned)base;
    }
    while (n > 0) emit(s, tmp[--n]);
}

static void emit_int64(sink_t* s, long long v) {
    if (v < 0) {
        emit(s, '-');
        emit_uint64(s, (unsigned long long)(-v), 10, 0);
    } else {
        emit_uint64(s, (unsigned long long)v, 10, 0);
    }
}

int vsnprintf(char* buf, size_t size, const char* fmt, va_list args) {
    sink_t s = { buf, size, 0 };

    for (const char* p = fmt; *p; p++) {
        if (*p != '%') { emit(&s, *p); continue; }
        p++;

        int longflag = 0;
        while (*p == 'l') { longflag++; p++; }

        switch (*p) {
            case 'd':
            case 'i':
                if (longflag >= 2) emit_int64(&s, va_arg(args, long long));
                else if (longflag == 1) emit_int(&s, va_arg(args, long));
                else emit_int(&s, va_arg(args, int));
                break;
            case 'u':
                if (longflag >= 2) emit_uint64(&s, va_arg(args, unsigned long long), 10, 0);
                else if (longflag == 1) emit_uint(&s, va_arg(args, unsigned long), 10, 0);
                else emit_uint(&s, va_arg(args, unsigned int), 10, 0);
                break;
            case 'x':
                if (longflag >= 2) emit_uint64(&s, va_arg(args, unsigned long long), 16, 0);
                else emit_uint(&s, va_arg(args, unsigned int), 16, 0);
                break;
            case 'X':
                if (longflag >= 2) emit_uint64(&s, va_arg(args, unsigned long long), 16, 1);
                else emit_uint(&s, va_arg(args, unsigned int), 16, 1);
                break;
            case 'p':
                emit_str(&s, "0x");
                emit_uint(&s, (unsigned long)va_arg(args, void*), 16, 0);
                break;
            case 's': {
                const char* str = va_arg(args, const char*);
                emit_str(&s, str ? str : "(null)");
                break;
            }
            case 'c':
                emit(&s, (char)va_arg(args, int));
                break;
            case '%':
                emit(&s, '%');
                break;
            default:
                emit(&s, '%');
                emit(&s, *p);
                break;
        }
    }

    if (size > 0) {
        size_t end = s.pos < size ? s.pos : size - 1;
        buf[end] = 0;
    }
    return (int)s.pos;
}

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int r = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return r;
}

int sprintf(char* buf, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int r = vsnprintf(buf, (size_t)-1, fmt, args);
    va_end(args);
    return r;
}
