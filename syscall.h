#ifndef SYSCALL_H
#define SYSCALL_H
#include <stdint.h>
#include "idt.h"

#define SYS_EXIT  1
#define SYS_WRITE 2
#define SYS_YIELD 3

#define SYS_COSFS_OPEN   4
#define SYS_COSFS_CLOSE  5
#define SYS_COSFS_READ   6
#define SYS_COSFS_WRITE  7
#define SYS_COSFS_LSEEK  8
#define SYS_COSFS_UNLINK 9
#define SYS_COSFS_MKDIR  10

#define SYS_FAT32_OPEN   11
#define SYS_FAT32_CLOSE  12
#define SYS_FAT32_READ   13
#define SYS_FAT32_WRITE  14
#define SYS_FAT32_LSEEK  15
#define SYS_FAT32_UNLINK 16
#define SYS_FAT32_MKDIR  17

uint32_t syscall_dispatch(registers_t *regs);

#endif
