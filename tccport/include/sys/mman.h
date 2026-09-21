#ifndef TCCPORT_SYS_MMAN_H
#define TCCPORT_SYS_MMAN_H
/* Kernel is identity-mapped, RWX everywhere: mprotect() is a no-op stub. */
#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4
int mprotect(void *addr, unsigned long len, int prot);
#endif
