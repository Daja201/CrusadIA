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

/* ---- vsnprintf ------------------------------------------------------
 * Supports: flags "-0+ #", width, .precision, '*', length hh h l ll z t j,
 * conversions d i u x X o c s p f (e/g are printed like f) and %%.
 * Always returns the length the full output would have (C99 semantics),
 * and vsnprintf(NULL, 0, ...) is allowed.
 */
typedef struct { char *buf; size_t size; size_t pos; } vs_out_t;

static void vs_put(vs_out_t *o, char c) {
    if (o->pos + 1 < o->size) o->buf[o->pos] = c;
    o->pos++;
}

static void vs_fill(vs_out_t *o, char c, int n) {
    while (n-- > 0) vs_put(o, c);
}

/* emit [prefix][zeros][body] padded to width */
static void vs_emit(vs_out_t *o, const char *prefix, int zeros, const char *body,
                    int blen, int width, int left, int zero_pad) {
    int plen = (int)strlen(prefix);
    int total = plen + zeros + blen;
    int pad = width > total ? width - total : 0;
    if (!left && !zero_pad) vs_fill(o, ' ', pad);
    for (int i = 0; i < plen; i++) vs_put(o, prefix[i]);
    if (!left && zero_pad) vs_fill(o, '0', pad);
    vs_fill(o, '0', zeros);
    for (int i = 0; i < blen; i++) vs_put(o, body[i]);
    if (left) vs_fill(o, ' ', pad);
}

int vsnprintf(char* buf, size_t size, const char* fmt, va_list args) {
    vs_out_t o = { buf, size, 0 };
    for (const char *f = fmt; *f; f++) {
        if (*f != '%') { vs_put(&o, *f); continue; }
        f++;
        int left = 0, zero = 0, plus = 0, space = 0, alt = 0;
        for (;; f++) {
            if (*f == '-') left = 1;
            else if (*f == '0') zero = 1;
            else if (*f == '+') plus = 1;
            else if (*f == ' ') space = 1;
            else if (*f == '#') alt = 1;
            else break;
        }
        int width = 0;
        if (*f == '*') {
            width = va_arg(args, int);
            if (width < 0) { left = 1; width = -width; }
            f++;
        } else {
            while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        }
        int prec = -1;
        if (*f == '.') {
            f++; prec = 0;
            if (*f == '*') { prec = va_arg(args, int); if (prec < 0) prec = -1; f++; }
            else while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0');
        }
        int lng = 0;   /* -2 hh, -1 h, 0 int, 1 long, 2 long long */
        for (;;) {
            if (*f == 'l') { lng++; f++; }
            else if (*f == 'h') { lng--; f++; }
            else if (*f == 'z' || *f == 't' || *f == 'j') { lng = 1; f++; }
            else break;
        }
        if (lng > 2) lng = 2;
        if (lng < -2) lng = -2;
        char conv = *f;
        if (!conv) break;

        switch (conv) {
            case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'p': {
                unsigned long long v;
                int neg = 0;
                int base = 10, upper = (conv == 'X');
                const char *prefix = "";
                if (conv == 'd' || conv == 'i') {
                    long long sv;
                    if (lng == 2) sv = va_arg(args, long long);
                    else if (lng == 1) sv = va_arg(args, long);
                    else { sv = va_arg(args, int); if (lng == -1) sv = (short)sv; else if (lng == -2) sv = (signed char)sv; }
                    if (sv < 0) { neg = 1; v = 0ULL - (unsigned long long)sv; } else v = (unsigned long long)sv;
                    prefix = neg ? "-" : plus ? "+" : space ? " " : "";
                } else {
                    if (conv == 'p') { v = (unsigned long)va_arg(args, void *); base = 16; prefix = "0x"; }
                    else {
                        if (lng == 2) v = va_arg(args, unsigned long long);
                        else if (lng == 1) v = va_arg(args, unsigned long);
                        else { v = va_arg(args, unsigned int); if (lng == -1) v = (unsigned short)v; else if (lng == -2) v = (unsigned char)v; }
                        if (conv == 'x' || conv == 'X') { base = 16; if (alt && v) prefix = upper ? "0X" : "0x"; }
                        else if (conv == 'o') { base = 8; if (alt) prefix = "0"; }
                    }
                }
                char tmp[24];
                int n = 0;
                const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
                if (v == 0) { if (prec != 0) tmp[n++] = '0'; }
                while (v > 0xFFFFFFFFULL) { tmp[n++] = digits[(unsigned)(v % (unsigned)base)]; v /= (unsigned)base; }
                { unsigned w = (unsigned)v; while (w) { tmp[n++] = digits[w % (unsigned)base]; w /= (unsigned)base; } }
                char body[24];
                for (int i = 0; i < n; i++) body[i] = tmp[n - 1 - i];
                int zeros = prec > n ? prec - n : 0;
                vs_emit(&o, prefix, zeros, body, n, width, left, zero && !left && prec < 0);
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                vs_emit(&o, "", 0, &c, 1, width, left, 0);
                break;
            }
            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                int n = 0;
                while (s[n] && (prec < 0 || n < prec)) n++;
                vs_emit(&o, "", 0, s, n, width, left, 0);
                break;
            }
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
                double d = va_arg(args, double);
                int p = prec < 0 ? 6 : prec;
                if (p > 9) p = 9;
                const char *prefix = "";
                char body[48];
                int n = 0;
                if (d != d) { body[n++] = 'n'; body[n++] = 'a'; body[n++] = 'n'; }
                else {
                    if (d < 0) { prefix = "-"; d = -d; } else if (plus) prefix = "+"; else if (space) prefix = " ";
                    if (d > 1.0e18) { body[n++] = 'i'; body[n++] = 'n'; body[n++] = 'f'; }
                    else {
                        unsigned long long ip = (unsigned long long)d;
                        double frac = d - (double)ip;
                        unsigned long long scale = 1;
                        for (int i = 0; i < p; i++) scale *= 10;
                        unsigned long long fp = (unsigned long long)(frac * (double)scale + 0.5);
                        if (fp >= scale) { ip++; fp -= scale; }
                        char t[24]; int tn = 0;
                        if (ip == 0) t[tn++] = '0';
                        while (ip) { t[tn++] = (char)('0' + (unsigned)(ip % 10)); ip /= 10; }
                        while (tn) body[n++] = t[--tn];
                        if (p > 0 || alt) body[n++] = '.';
                        char ft[12];
                        for (int i = p - 1; i >= 0; i--) { ft[i] = (char)('0' + (unsigned)(fp % 10)); fp /= 10; }
                        for (int i = 0; i < p; i++) body[n++] = ft[i];
                    }
                }
                vs_emit(&o, prefix, 0, body, n, width, left, zero && !left);
                break;
            }
            case '%':
                vs_put(&o, '%');
                break;
            default:
                vs_put(&o, '%');
                vs_put(&o, conv);
                break;
        }
    }
    if (size > 0) buf[o.pos < size ? o.pos : size - 1] = '\0';
    return (int)o.pos;
}

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}