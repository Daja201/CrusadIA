#include "string.h"
#include "stdint.h"
#include "heap.h"
#include "task.h"

//compares 2 strings by ASCII character valuables 
int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

//looks for last character 
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

//copies string into destination array
char *strcpy(char *dest, const char *src) {
    char *save = dest;
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
    return save;
}

//counts lenght of string
size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

//copies a specified number of bytes from a source to some place
void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char*)dest;
    const unsigned char *s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dest;
}

// can write bytes of memory into virtual mem when pagings enabled
void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char*)s;
    while (n--) *p++ = (unsigned char)c;'=';
    return s;
}

//returns difference between bytes
//difference as number between a and b
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = (const unsigned char*)a;
    const unsigned char *pb = (const unsigned char*)b;
    while (n--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}

//converts integers into ascii
//value –> writes into buffer as base (decimal etc)
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

// copies source string at the end to destination 
// finds end of dest and copies src to it
char *strcat(char *dest, const char *src) {
    char *ptr = dest;
    while (*ptr) ptr++;
    while (*src) {
        *ptr++ = *src++;
    }
    *ptr = '\0';
    return dest;
}

//string to integer converter
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

//copies n chars of src to dest while padding the rest with '\0'
char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

//ascii to integer
int atoi(const char* s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' || *s == '\v') s++;
    int sign = 1;
    if (*s == '+') s++; else if (*s == '-') { sign = -1; s++; }
    long val = 0;
    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
    return (int)(val * sign);
}

//string to long integer
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

//case insensitive string comparison
int strcasecmp(const char* a, const char* b) {
    while (*a && *b) {
        unsigned char ca = (unsigned char)tolower(*a);
        unsigned char cb = (unsigned char)tolower(*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)tolower(*a) - (unsigned char)tolower(*b);
}

char* strdup(const char* s) {
    if (!s) return 0;
    size_t len = strlen(s) + 1;
    char* out = (char*)malloc(len);
    if (!out) return 0;
    memcpy(out, s, len);
    return out;
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == 0) ? (char*)s : 0;
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)haystack;
    }
    return 0;
}

int strncmp(const char* a, const char* b, size_t n) {
    while (n && *a && (*a == *b)) {
        a++; b++; n--;
    }
    if (n == 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

void qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*)) {
    uint8_t* arr = (uint8_t*)base;
    for (size_t i = 1; i < nmemb; i++) {
        size_t j = i;
        while (j > 0) {
            void* a = arr + (j - 1) * size;
            void* b = arr + j * size;
            if (compar(a, b) <= 0) break;
            for (size_t k = 0; k < size; k++) {
                uint8_t tmp = ((uint8_t*)a)[k];
                ((uint8_t*)a)[k] = ((uint8_t*)b)[k];
                ((uint8_t*)b)[k] = tmp;
            }
            j--;
        }
    }
}

void* bsearch(const void* key, const void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*)) {
    const uint8_t* arr = (const uint8_t*)base;
    size_t lo = 0, hi = nmemb;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const void* elem = arr + mid * size;
        int cmp = compar(key, elem);
        if (cmp == 0) return (void*)elem;
        if (cmp < 0) hi = mid;
        else lo = mid + 1;
    }
    return 0;
}

int abs(int n) {
    return n < 0 ? -n : n;
}

void exit(int code) {
    (void)code;
    task_exit();
}

void abort(void) {
    task_exit();
}

static void vsnprintf_put(char* buf, size_t size, size_t* pos, char c) {
    if (*pos + 1 < size) buf[*pos] = c;
    (*pos)++;
}

static void vsnprintf_puts(char* buf, size_t size, size_t* pos, const char* s) {
    while (*s) vsnprintf_put(buf, size, pos, *s++);
}

int vsnprintf(char* buf, size_t size, const char* fmt, va_list args) {
    size_t pos = 0;
    char numbuf[32];
    for (size_t i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] != '%') {
            vsnprintf_put(buf, size, &pos, fmt[i]);
            continue;
        }
        i++;
        int is_long = 0;
        if (fmt[i] == 'l') { is_long = 1; i++; }
        switch (fmt[i]) {
            case 'd': {
                long val = is_long ? va_arg(args, long) : (long)va_arg(args, int);
                itoa((int)val, numbuf, 10);
                vsnprintf_puts(buf, size, &pos, numbuf);
                break;
            }
            case 'u': {
                unsigned long val = is_long ? va_arg(args, unsigned long) : (unsigned long)va_arg(args, unsigned int);
                char tmp[32];
                int idx = 0;
                if (val == 0) tmp[idx++] = '0';
                while (val) { tmp[idx++] = '0' + (val % 10); val /= 10; }
                while (idx > 0) vsnprintf_put(buf, size, &pos, tmp[--idx]);
                break;
            }
            case 'x': {
                unsigned long val = is_long ? va_arg(args, unsigned long) : (unsigned long)va_arg(args, unsigned int);
                itoa((int)val, numbuf, 16);
                vsnprintf_puts(buf, size, &pos, numbuf);
                break;
            }
            case 'p': {
                unsigned long val = (unsigned long)va_arg(args, void*);
                vsnprintf_puts(buf, size, &pos, "0x");
                itoa((int)val, numbuf, 16);
                vsnprintf_puts(buf, size, &pos, numbuf);
                break;
            }
            case 's': {
                char* s = va_arg(args, char*);
                vsnprintf_puts(buf, size, &pos, s ? s : "(null)");
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                vsnprintf_put(buf, size, &pos, c);
                break;
            }
            case '%':
                vsnprintf_put(buf, size, &pos, '%');
                break;
            default:
                vsnprintf_put(buf, size, &pos, '%');
                vsnprintf_put(buf, size, &pos, fmt[i]);
                break;
        }
    }
    if (size > 0) buf[pos < size ? pos : size - 1] = '\0';
    return (int)pos;
}

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}