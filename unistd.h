#ifndef UNISTD_H
#define UNISTD_H
#include <stddef.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int close(int fd);
long read(int fd, void* buf, size_t count);
long write(int fd, const void* buf, size_t count);
long lseek(int fd, long offset, int whence);
int unlink(const char* path);
char* getcwd(char* buf, size_t size);

#endif
