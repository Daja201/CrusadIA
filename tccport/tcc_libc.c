/*
 * tcc_libc.c - the handful of libc functions TinyCC needs that CrusadIA lacks.
 *
 * Everything here is deliberately tiny. It is compiled with
 * -fno-tree-loop-distribute-patterns (see Makefile) so GCC never turns the
 * loops below back into calls to memmove()/memcpy() (= infinite recursion).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <stdint.h>
#include <ctype.h>
#include <unistd.h>
#include "klog.h"
#include "rtc.h"

/* ------------------------------------------------------------------ misc */

int errno;
static char *empty_environ[1] = { 0 };
char **environ = empty_environ;

char *getenv(const char *name) { (void)name; return 0; }

/* CrusadIA has no path resolution; just hand back a copy of the name. */
char *realpath(const char *path, char *resolved) {
    size_t n = strlen(path);
    if (!resolved) resolved = (char *)malloc(n + 1);
    if (!resolved) return 0;
    memcpy(resolved, path, n + 1);
    return resolved;
}

char *strerror(int e) {
    switch (e) {
        case ENOENT: return "no such file";
        case ENOMEM: return "out of memory";
        case EINVAL: return "invalid argument";
        case ERANGE: return "out of range";
        default:     return "error";
    }
}

/* The kernel is identity mapped, read/write/execute everywhere (no NX). */
int mprotect(void *addr, unsigned long len, int prot) {
    (void)addr; (void)len; (void)prot;
    return 0;
}

/* ---------------------------------------------------------------- string */

void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) return dest;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    while (n--) {
        if (*p == (unsigned char)c) return (void *)p;
        p++;
    }
    return 0;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *d = dest + strlen(dest);
    while (n-- && *src) *d++ = *src++;
    *d = 0;
    return dest;
}

/* ------------------------------------------------------- number parsing */

static unsigned long long parse_u64(const char *s, char **end, int base,
                                    int *neg, int *overflow) {
    const char *p = s;
    unsigned long long v = 0;
    int any = 0;
    *neg = 0; *overflow = 0;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '-') { *neg = 1; p++; }
    else if (*p == '+') p++;
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')
        && isxdigit((unsigned char)p[2])) {
        p += 2; base = 16;
    } else if (base == 0) {
        base = (p[0] == '0') ? 8 : 10;
    }
    for (;; p++) {
        int d;
        if (*p >= '0' && *p <= '9') d = *p - '0';
        else if (*p >= 'a' && *p <= 'z') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z') d = *p - 'A' + 10;
        else break;
        if (d >= base) break;
        if (v > (0xFFFFFFFFFFFFFFFFULL - d) / base) *overflow = 1;
        v = v * base + d;
        any = 1;
    }
    if (end) *end = (char *)(any ? p : s);
    return v;
}

unsigned long long strtoull(const char *s, char **end, int base) {
    int neg, ovf;
    unsigned long long v = parse_u64(s, end, base, &neg, &ovf);
    if (ovf) { errno = ERANGE; return 0xFFFFFFFFFFFFFFFFULL; }
    return neg ? (unsigned long long)(0 - v) : v;
}

long long strtoll(const char *s, char **end, int base) {
    int neg, ovf;
    unsigned long long v = parse_u64(s, end, base, &neg, &ovf);
    if (ovf || (!neg && v > 0x7FFFFFFFFFFFFFFFULL) || (neg && v > 0x8000000000000000ULL)) {
        errno = ERANGE;
        return neg ? (long long)0x8000000000000000ULL : 0x7FFFFFFFFFFFFFFFLL;
    }
    return neg ? (long long)(0 - v) : (long long)v;
}

unsigned long strtoul(const char *s, char **end, int base) {
    int neg, ovf;
    unsigned long long v = parse_u64(s, end, base, &neg, &ovf);
    if (ovf || v > 0xFFFFFFFFULL) { errno = ERANGE; return 0xFFFFFFFFUL; }
    return neg ? (unsigned long)(0 - (unsigned long)v) : (unsigned long)v;
}

/* ------------------------------------------------- floating point (x87) */
/* The FPU must be initialised (fninit) before any of this runs;
 * tcc_kernel.c does that in tcc_os_fpu_init(). */

long double ldexpl(long double x, int e) {
    long double r;
    /* st(0) = x * 2^trunc(st(1)) */
    __asm__ ("fscale" : "=t"(r) : "0"(x), "u"((long double)e));
    return r;
}

double ldexp(double x, int e) { return (double)ldexpl((long double)x, e); }

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int ci_prefix(const char *p, const char *word) {
    while (*word) {
        if (tolower((unsigned char)*p) != *word) return 0;
        p++; word++;
    }
    return 1;
}

long double strtold(const char *s, char **end) {
    const char *p = s;
    int neg = 0;
    long double v = 0.0L;

    while (isspace((unsigned char)*p)) p++;
    if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;

    if (ci_prefix(p, "inf")) {
        if (end) *end = (char *)(p + (ci_prefix(p, "infinity") ? 8 : 3));
        v = 1.0L; v = ldexpl(v, 20000);            /* overflows to +inf */
        return neg ? -v : v;
    }

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        (hexval(p[2]) >= 0 || (p[2] == '.' && hexval(p[3]) >= 0))) {
        /* hexadecimal floating constant: 0x1.8p3 */
        int exp2 = 0, any = 0;
        p += 2;
        while (hexval(*p) >= 0) { v = v * 16.0L + hexval(*p++); any = 1; }
        if (*p == '.') {
            p++;
            while (hexval(*p) >= 0) { v = v * 16.0L + hexval(*p++); exp2 -= 4; any = 1; }
        }
        if (any && (*p == 'p' || *p == 'P')) {
            const char *q = p + 1;
            int eneg = 0, e = 0;
            if (*q == '-') { eneg = 1; q++; } else if (*q == '+') q++;
            if (*q >= '0' && *q <= '9') {
                while (*q >= '0' && *q <= '9') { if (e < 100000) e = e * 10 + (*q - '0'); q++; }
                exp2 += eneg ? -e : e;
                p = q;
            }
        }
        if (end) *end = (char *)(any ? p : s);
        v = ldexpl(v, exp2);
        return neg ? -v : v;
    }

    {
        int exp10 = 0, any = 0, sig = 0;
        while (*p >= '0' && *p <= '9') {
            any = 1;
            if (sig < 30) { v = v * 10.0L + (*p - '0'); if (v != 0.0L) sig++; }
            else exp10++;
            p++;
        }
        if (*p == '.') {
            p++;
            while (*p >= '0' && *p <= '9') {
                any = 1;
                if (sig < 30) { v = v * 10.0L + (*p - '0'); if (v != 0.0L) sig++; exp10--; }
                p++;
            }
        }
        if (!any) { if (end) *end = (char *)s; return 0.0L; }
        if (*p == 'e' || *p == 'E') {
            const char *q = p + 1;
            int eneg = 0, e = 0;
            if (*q == '-') { eneg = 1; q++; } else if (*q == '+') q++;
            if (*q >= '0' && *q <= '9') {
                while (*q >= '0' && *q <= '9') { if (e < 100000) e = e * 10 + (*q - '0'); q++; }
                exp10 += eneg ? -e : e;
                p = q;
            }
        }
        if (end) *end = (char *)p;
        if (v != 0.0L && exp10 != 0) {
            long double scale = 1.0L, b = 10.0L;
            int e = exp10 < 0 ? -exp10 : exp10;
            if (e > 5000) e = 5000;
            while (e) { if (e & 1) scale *= b; b *= b; e >>= 1; }
            v = exp10 < 0 ? v / scale : v * scale;
        }
        return neg ? -v : v;
    }
}

double strtod(const char *s, char **end) { return (double)strtold(s, end); }
float  strtof(const char *s, char **end) { return (float)strtold(s, end); }

/* ------------------------------------------------------------------ time */

static long days_from_civil(long y, unsigned m, unsigned d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

time_t time(time_t *t) {
    int Y, M, D, h, m, s;
    rtc_get_datetime(&Y, &M, &D, &h, &m, &s);
    if (Y < 100) Y += 2000;   /* RTC drivers sometimes return 2 digits */
    time_t r = days_from_civil(Y, (unsigned)M, (unsigned)D) * 86400L + h * 3600L + m * 60L + s;
    if (t) *t = r;
    return r;
}

struct tm *localtime(const time_t *tp) {
    static struct tm tmv;
    long t = *tp;
    long days = t / 86400, rem = t % 86400;
    if (rem < 0) { rem += 86400; days--; }
    tmv.tm_hour = (int)(rem / 3600);
    tmv.tm_min  = (int)((rem % 3600) / 60);
    tmv.tm_sec  = (int)(rem % 60);
    tmv.tm_wday = (int)((days + 4) % 7); if (tmv.tm_wday < 0) tmv.tm_wday += 7;
    /* civil_from_days */
    long z = days + 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y++;
    tmv.tm_year = (int)(y - 1900);
    tmv.tm_mon  = (int)m - 1;
    tmv.tm_mday = (int)d;
    tmv.tm_yday = (int)(days - days_from_civil(y, 1, 1));
    tmv.tm_isdst = 0;
    return &tmv;
}

int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (tv) { tv->tv_sec = time(0); tv->tv_usec = 0; }
    return 0;
}

/* ----------------------------------------------------------------- stdio */
/* stdout/stderr are printed on the CrusadIA terminal via klog().
 * Real files go through the FILE functions in fs.c. */

static FILE std_streams[3];
FILE *stdin  = &std_streams[0];
FILE *stdout = &std_streams[1];
FILE *stderr = &std_streams[2];

static int is_console(FILE *f) { return f == stdout || f == stderr; }

int vfprintf(FILE *f, const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(0, 0, fmt, ap2);
    va_end(ap2);
    if (n < 0) return n;
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) return -1;
    vsnprintf(buf, (size_t)n + 1, fmt, ap);
    if (is_console(f)) klog(buf);
    else if (f) fwrite(buf, 1, (size_t)n, f);
    free(buf);
    return n;
}

int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, 0x7FFFFFFF, fmt, ap);
    va_end(ap);
    return n;
}

int fputs(const char *s, FILE *f) {
    if (is_console(f)) { klog(s); return 0; }
    size_t n = strlen(s);
    return (f && fwrite(s, 1, n, f) == n) ? 0 : EOF;
}

int fputc(int c, FILE *f) {
    char b[2] = { (char)c, 0 };
    if (is_console(f)) { klog(b); return c; }
    return (f && fwrite(b, 1, 1, f) == 1) ? c : EOF;
}

int puts(const char *s)  { klog(s); klog("\n"); return 0; }
int putchar(int c)       { return fputc(c, stdout); }
int fflush(FILE *f)      { (void)f; return 0; }   /* fs.c writes through */
int remove(const char *p){ return unlink(p); }

/* Not supported: writing ELF/objects to a file (-o / tcc_output_file()). */
FILE *fdopen(int fd, const char *mode)               { (void)fd; (void)mode; return 0; }
FILE *freopen(const char *p, const char *m, FILE *f) { (void)p; (void)m; (void)f; return 0; }
