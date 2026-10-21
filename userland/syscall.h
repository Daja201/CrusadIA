#ifndef USERLAND_SYSCALL_H
#define USERLAND_SYSCALL_H

#define SYS_EXIT  1
#define SYS_WRITE 2
#define SYS_YIELD 3

#define SYS_OPEN   4
#define SYS_CLOSE  5
#define SYS_READ   6
#define SYS_FWRITE 7
#define SYS_LSEEK  8
#define SYS_UNLINK 9
#define SYS_MKDIR  10

#define SYS_BRK   18
#define SYS_SPAWN 19
#define SYS_GETTIME 20

unsigned int _syscall(unsigned int num, unsigned int a1, unsigned int a2, unsigned int a3);

#endif
