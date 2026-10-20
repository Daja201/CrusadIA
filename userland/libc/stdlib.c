#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char* getenv(const char* name) {
    if (!environ) return NULL;
    size_t len = strlen(name);
    for (char** e = environ; *e; e++) {
        if (strncmp(*e, name, len) == 0 && (*e)[len] == '=') return *e + len + 1;
    }
    return NULL;
}

int atoi(const char* s) {
    return (int)strtol(s, NULL, 10);
}

long atol(const char* s) {
    return strtol(s, NULL, 10);
}

long strtol(const char* nptr, char** endptr, int base) {
    const char* p = nptr;
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;

    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    else if (*p == '+') p++;

    if (base == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
        else if (p[0] == '0') { base = 8; p++; }
        else base = 10;
    } else if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }

    long result = 0;
    while (*p) {
        int digit;
        if (*p >= '0' && *p <= '9') digit = *p - '0';
        else if (*p >= 'a' && *p <= 'z') digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z') digit = *p - 'A' + 10;
        else break;
        if (digit >= base) break;
        result = result * base + digit;
        p++;
    }

    if (endptr) *endptr = (char*)p;
    return neg ? -result : result;
}

void exit(int code) {
    _exit(code);
}

void abort(void) {
    _exit(134);
}

int abs(int n) {
    return n < 0 ? -n : n;
}

static void swap_bytes(void* a, void* b, size_t size) {
    unsigned char* pa = a;
    unsigned char* pb = b;
    for (size_t i = 0; i < size; i++) {
        unsigned char t = pa[i];
        pa[i] = pb[i];
        pb[i] = t;
    }
}

void qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*)) {
    unsigned char* arr = base;
    if (nmemb < 2) return;
    for (size_t i = 0; i < nmemb - 1; i++) {
        for (size_t j = 0; j < nmemb - 1 - i; j++) {
            void* a = arr + j * size;
            void* b = arr + (j + 1) * size;
            if (compar(a, b) > 0) swap_bytes(a, b, size);
        }
    }
}

void* bsearch(const void* key, const void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*)) {
    const unsigned char* arr = base;
    size_t lo = 0, hi = nmemb;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const void* elem = arr + mid * size;
        int c = compar(key, elem);
        if (c == 0) return (void*)elem;
        if (c < 0) hi = mid;
        else lo = mid + 1;
    }
    return NULL;
}
