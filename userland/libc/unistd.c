#include <unistd.h>
#include "../syscall.h"

int open(const char* path, int flags) {
    return (int)_syscall(SYS_OPEN, (unsigned int)path, (unsigned int)flags, 0);
}

int close(int fd) {
    return (int)_syscall(SYS_CLOSE, (unsigned int)fd, 0, 0);
}

long read(int fd, void* buf, size_t count) {
    return (long)_syscall(SYS_READ, (unsigned int)fd, (unsigned int)buf, (unsigned int)count);
}

long write(int fd, const void* buf, size_t count) {
    return (long)_syscall(SYS_FWRITE, (unsigned int)fd, (unsigned int)buf, (unsigned int)count);
}

long lseek(int fd, long offset, int whence) {
    return (long)_syscall(SYS_LSEEK, (unsigned int)fd, (unsigned int)offset, (unsigned int)whence);
}

int unlink(const char* path) {
    return (int)_syscall(SYS_UNLINK, (unsigned int)path, 0, 0);
}

int mkdir(const char* path) {
    return (int)_syscall(SYS_MKDIR, (unsigned int)path, 0, 0);
}

static unsigned int g_cur_break = 0;

void* sbrk(int increment) {
    if (g_cur_break == 0) {
        g_cur_break = _syscall(SYS_BRK, 0, 0, 0);
    }
    if (increment == 0) return (void*)g_cur_break;

    unsigned int old_break = g_cur_break;
    unsigned int wanted = old_break + (unsigned int)increment;
    unsigned int got = _syscall(SYS_BRK, wanted, 0, 0);
    if (got != wanted) return (void*)-1;

    g_cur_break = got;
    return (void*)old_break;
}

void _exit(int code) {
    _syscall(SYS_EXIT, (unsigned int)code, 0, 0);
    for (;;) { }
}

int spawn(const char* path, char* const argv[], int argc) {
    int r = (int)_syscall(SYS_SPAWN, (unsigned int)path, (unsigned int)argv, (unsigned int)argc);
    return r;
}

char** environ = (char**)0;

void __init_environ(char** envp) {
    environ = envp;
}
