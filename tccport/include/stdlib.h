#ifndef TCCPORT_STDLIB_H
#define TCCPORT_STDLIB_H
/* host-side shim so tinycc/*.c compile against the CrusadIA kernel "libc" */
#include <stddef.h>
#include <heap.h>      /* malloc free calloc realloc */
#include <string.h>    /* qsort bsearch abs exit abort atoi strtol itoa */
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
unsigned long      strtoul (const char *nptr, char **endptr, int base);
long long          strtoll (const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
double      strtod (const char *nptr, char **endptr);
float       strtof (const char *nptr, char **endptr);
long double strtold(const char *nptr, char **endptr);
extern char **environ;
char *getenv(const char *name);
char *realpath(const char *path, char *resolved);
#endif
