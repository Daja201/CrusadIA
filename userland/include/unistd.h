#ifndef USERLAND_UNISTD_H
#define USERLAND_UNISTD_H
#include <stddef.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

int open(const char* path, int flags);
int close(int fd);
long read(int fd, void* buf, size_t count);
long write(int fd, const void* buf, size_t count);
long lseek(int fd, long offset, int whence);
int unlink(const char* path);
int mkdir(const char* path);
void* sbrk(int increment);
void _exit(int code);
int spawn(const char* path, char* const argv[], int argc);

extern char** environ;
void __init_environ(char** envp);

#endif
