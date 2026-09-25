#ifndef RING3FS_H
#define RING3FS_H
#include <stdint.h>
#include <stddef.h>

int r3_cosfs_open(const char* path, int flags);
int r3_cosfs_close(int fd);
long r3_cosfs_read(int fd, void* buf, uint32_t count);
long r3_cosfs_write(int fd, const void* buf, uint32_t count);
long r3_cosfs_lseek(int fd, long offset, int whence);
int r3_cosfs_unlink(const char* path);
int r3_cosfs_mkdir(const char* name);

int r3_fat32_open(const char* path, int flags);
int r3_fat32_close(int fd);
long r3_fat32_read(int fd, void* buf, uint32_t count);
long r3_fat32_write(int fd, const void* buf, uint32_t count);
long r3_fat32_lseek(int fd, long offset, int whence);
int r3_fat32_unlink(const char* path);
int r3_fat32_mkdir(const char* path);

#endif
