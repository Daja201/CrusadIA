#include "ring3fs.h"
#include "syscall.h"

static inline uint32_t r3_syscall(uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3) {
    uint32_t ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2), "d"(a3) : "memory");
    return ret;
}

int r3_cosfs_open(const char* path, int flags) {
    return (int)r3_syscall(SYS_COSFS_OPEN, (uint32_t)path, (uint32_t)flags, 0);
}

int r3_cosfs_close(int fd) {
    return (int)r3_syscall(SYS_COSFS_CLOSE, (uint32_t)fd, 0, 0);
}

long r3_cosfs_read(int fd, void* buf, uint32_t count) {
    return (long)r3_syscall(SYS_COSFS_READ, (uint32_t)fd, (uint32_t)buf, count);
}

long r3_cosfs_write(int fd, const void* buf, uint32_t count) {
    return (long)r3_syscall(SYS_COSFS_WRITE, (uint32_t)fd, (uint32_t)buf, count);
}

long r3_cosfs_lseek(int fd, long offset, int whence) {
    return (long)r3_syscall(SYS_COSFS_LSEEK, (uint32_t)fd, (uint32_t)offset, (uint32_t)whence);
}

int r3_cosfs_unlink(const char* path) {
    return (int)r3_syscall(SYS_COSFS_UNLINK, (uint32_t)path, 0, 0);
}

int r3_cosfs_mkdir(const char* name) {
    return (int)r3_syscall(SYS_COSFS_MKDIR, (uint32_t)name, 0, 0);
}

int r3_fat32_open(const char* path, int flags) {
    return (int)r3_syscall(SYS_FAT32_OPEN, (uint32_t)path, (uint32_t)flags, 0);
}

int r3_fat32_close(int fd) {
    return (int)r3_syscall(SYS_FAT32_CLOSE, (uint32_t)fd, 0, 0);
}

long r3_fat32_read(int fd, void* buf, uint32_t count) {
    return (long)r3_syscall(SYS_FAT32_READ, (uint32_t)fd, (uint32_t)buf, count);
}

long r3_fat32_write(int fd, const void* buf, uint32_t count) {
    return (long)r3_syscall(SYS_FAT32_WRITE, (uint32_t)fd, (uint32_t)buf, count);
}

long r3_fat32_lseek(int fd, long offset, int whence) {
    return (long)r3_syscall(SYS_FAT32_LSEEK, (uint32_t)fd, (uint32_t)offset, (uint32_t)whence);
}

int r3_fat32_unlink(const char* path) {
    return (int)r3_syscall(SYS_FAT32_UNLINK, (uint32_t)path, 0, 0);
}

int r3_fat32_mkdir(const char* path) {
    return (int)r3_syscall(SYS_FAT32_MKDIR, (uint32_t)path, 0, 0);
}
