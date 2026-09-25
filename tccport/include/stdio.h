#ifndef TCCPORT_STDIO_H
#define TCCPORT_STDIO_H
#include <stddef.h>
#include <stdarg.h>
#include <fs.h>    
#ifndef TCCPORT_SSIZE_T
#define TCCPORT_SSIZE_T
typedef long ssize_t;
#endif
#define EOF (-1)
#define BUFSIZ 512
extern FILE *stdin, *stdout, *stderr;
int   printf  (const char *fmt, ...);
int   fprintf (FILE *f, const char *fmt, ...);
int   sprintf (char *buf, const char *fmt, ...);
int   vfprintf(FILE *f, const char *fmt, va_list ap);
int   fputs   (const char *s, FILE *f);
int   fputc   (int c, FILE *f);
int   puts    (const char *s);
int   putchar (int c);
int   fflush  (FILE *f);
FILE *fdopen  (int fd, const char *mode);
FILE *freopen (const char *path, const char *mode, FILE *stream);
int   remove  (const char *path);
#endif
