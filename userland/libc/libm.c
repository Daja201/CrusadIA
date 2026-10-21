#include <math.h>

double fabs(double x) {
    unsigned long long bits;
    __builtin_memcpy(&bits, &x, sizeof(bits));
    bits &= 0x7FFFFFFFFFFFFFFFULL;
    __builtin_memcpy(&x, &bits, sizeof(bits));
    return x;
}
