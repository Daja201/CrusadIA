#ifndef USERLAND_MATH_H
#define USERLAND_MATH_H

#define INFINITY (__builtin_inff())
#define NAN      (__builtin_nanf(""))
#define HUGE_VAL (__builtin_huge_val())

double fabs(double x);

#endif
