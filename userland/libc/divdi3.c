typedef unsigned long long u64;
typedef long long s64;

u64 __udivdi3(u64 num, u64 den) {
    if (den == 0) return 0;
    u64 quot = 0;
    u64 rem = 0;
    int i;
    for (i = 63; i >= 0; i--) {
        rem = (rem << 1) | ((num >> i) & 1ULL);
        if (rem >= den) {
            rem -= den;
            quot |= (1ULL << i);
        }
    }
    return quot;
}

u64 __umoddi3(u64 num, u64 den) {
    if (den == 0) return 0;
    u64 rem = 0;
    int i;
    for (i = 63; i >= 0; i--) {
        rem = (rem << 1) | ((num >> i) & 1ULL);
        if (rem >= den) {
            rem -= den;
        }
    }
    return rem;
}

s64 __divdi3(s64 num, s64 den) {
    int neg = 0;
    u64 un, ud;
    if (num < 0) { un = (u64)(-num); neg ^= 1; } else { un = (u64)num; }
    if (den < 0) { ud = (u64)(-den); neg ^= 1; } else { ud = (u64)den; }
    u64 q = __udivdi3(un, ud);
    return neg ? -(s64)q : (s64)q;
}

s64 __moddi3(s64 num, s64 den) {
    int neg = 0;
    u64 un, ud;
    if (num < 0) { un = (u64)(-num); neg = 1; } else { un = (u64)num; }
    if (den < 0) { ud = (u64)(-den); } else { ud = (u64)den; }
    u64 r = __umoddi3(un, ud);
    return neg ? -(s64)r : (s64)r;
}
