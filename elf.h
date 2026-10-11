#ifndef ELF_H
#define ELF_H
#include <stdint.h>

#define ELF_OK          0
#define ELF_E_OPEN     -1
#define ELF_E_READ     -2
#define ELF_E_MAGIC    -3
#define ELF_E_CLASS    -4
#define ELF_E_TYPE     -5
#define ELF_E_PHDR     -6
#define ELF_E_SEGMENT  -7
#define ELF_E_MEM      -8

int elf_exec(const char* path, uint32_t priority);
const char* elf_strerror(int err);

#endif
