#ifndef SYS_STAT_H
#define SYS_STAT_H
#include <stdint.h>

#define S_IFREG 0x8000
#define S_IFDIR 0x4000

struct stat {
    uint32_t st_size;
    uint16_t st_mode;
};

int stat(const char* path, struct stat* st);
int fstat(int fd, struct stat* st);

#endif
