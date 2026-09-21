#ifndef TCCPORT_STRING_H
#define TCCPORT_STRING_H
/* CrusadIA's own string.h plus the few libc functions it does not have yet */
#include_next <string.h>
void *memmove(void *dest, const void *src, size_t n);
void *memchr (const void *s, int c, size_t n);
char *strncat(char *dest, const char *src, size_t n);
char *strerror(int errnum);
#endif
