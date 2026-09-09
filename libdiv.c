#include <stdint.h>

unsigned long long __udivdi3(unsigned long long a, unsigned long long b) {
    if (b == 0) return ~0ULL;
    unsigned long long q = 0;
    unsigned long long r = 0;
    for (int i = 63; i >= 0; --i) {
        r = (r << 1) | ((a >> i) & 1ULL);
        if (r >= b) {
            r -= b;
            q |= (1ULL << i);
        }
    }
    return q;
}

unsigned long long __umoddi3(unsigned long long a, unsigned long long b) {
    if (b == 0) return a;
    unsigned long long q = 0;
    unsigned long long r = 0;
    for (int i = 63; i >= 0; --i) {
        r = (r << 1) | ((a >> i) & 1ULL);
        if (r >= b) {
            r -= b;
            q |= (1ULL << i);
        }
    }
    return r;
}
