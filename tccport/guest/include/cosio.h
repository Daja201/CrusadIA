#ifndef COSIO_H
#define COSIO_H

int cosfs_open(const char* path, int flags);
int cosfs_close(int fd);
long cosfs_read(int fd, void* buf, unsigned int count);
long cosfs_write(int fd, const void* buf, unsigned int count);
long cosfs_lseek(int fd, long offset, int whence);
int cosfs_unlink(const char* path);
int cosfs_mkdir(const char* name);

int fat32_open(const char* path, int flags);
int fat32_close(int fd);
long fat32_read(int fd, void* buf, unsigned int count);
long fat32_write(int fd, const void* buf, unsigned int count);
long fat32_lseek(int fd, long offset, int whence);
int fat32_unlink(const char* path);
int fat32_mkdir(const char* path);

#endif