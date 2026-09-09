#include "string.h"
#include "stdint.h"

int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

char* strrchr(const char* s, int c) {
    char* last = NULL;
    if (s == NULL) return NULL;
    
    while (*s != '\0') {
        if (*s == (char)c) {
            last = (char*)s;
        }
        s++;
    }
    
    if ((char)c == '\0') {
        return (char*)s;
    }
    
    return last;
}

char *strcpy(char *dest, const char *src) {
    char *save = dest;
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
    return save;
}

size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char*)dest;
    const unsigned char *s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dest;
}

void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char*)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = (const unsigned char*)a;
    const unsigned char *pb = (const unsigned char*)b;
    while (n--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}

char* itoa(int value, char* buffer, int base) {
    if (base < 2 || base > 16) {
        buffer[0] = 0;
        return buffer;
    }
    char *ptr = buffer;
    char *ptr1 = buffer;
    char tmp_char;
    int tmp_value;

    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = 0;
        return buffer;
    }
    int sign = 0;
    if (value < 0 && base == 10) {
        sign = 1;
        value = -value;
    }
    while (value != 0) {
        tmp_value = value % base;
        *ptr++ = (tmp_value < 10)
            ? tmp_value + '0'
            : tmp_value - 10 + 'A';
        value /= base;
    }
    if (sign)
        *ptr++ = '-';

    *ptr-- = 0;
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
    return buffer;
}

char *strcat(char *dest, const char *src) {
    char *ptr = dest;
    while (*ptr) ptr++;
    while (*src) {
        *ptr++ = *src++;
    }
    *ptr = '\0';
    return dest;
}

int str_to_int(const char *str) {
    int res = 0;
    for (int i = 0; str[i] != '\0'; ++i) {
        if (str[i] >= '0' && str[i] <= '9') {
            res = res * 10 + (str[i] - '0');
        } else {
            break;
        }
    }
    return res;
}

char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

int atoi(const char* s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' || *s == '\v') s++;
    int sign = 1;
    if (*s == '+') s++; else if (*s == '-') { sign = -1; s++; }
    long val = 0;
    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
    return (int)(val * sign);
}

long strtol(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' || *s == '\v') s++;
    int sign = 1;
    if (*s == '+') s++; else if (*s == '-') { sign = -1; s++; }
    if (base == 0) {
        if (*s == '0') {
            if (s[1] == 'x' || s[1] == 'X') base = 16;
            else base = 8;
        } else base = 10;
    }
    if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    long acc = 0;
    const char* start = s;
    while (*s) {
        int digit;
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'z') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') digit = *s - 'A' + 10;
        else break;
        if (digit >= base) break;
        acc = acc * base + digit;
        s++;
    }
    if (endptr) *endptr = (char*)(s == start ? nptr : s);
    return acc * sign;
}

int strcasecmp(const char* a, const char* b) {
    while (*a && *b) {
        unsigned char ca = (unsigned char)tolower(*a);
        unsigned char cb = (unsigned char)tolower(*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)tolower(*a) - (unsigned char)tolower(*b);
}