#include "elf.h"
#include "vfs.h"
#include "fcntl.h"
#include "paging.h"
#include "pmm.h"
#include "task.h"
#include "string.h"

#define EI_NIDENT   16
#define ELFMAG0     0x7F
#define ELFMAG1     'E'
#define ELFMAG2     'L'
#define ELFMAG3     'F'
#define ELFCLASS32  1
#define ELFDATA2LSB 1
#define ET_EXEC     2
#define EM_386      3
#define PT_LOAD     1
#define PAGE_SIZE   4096

typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf32_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} elf32_phdr_t;

const char* elf_strerror(int err) {
    switch (err) {
        case ELF_OK:        return "ok";
        case ELF_E_OPEN:    return "cannot open file";
        case ELF_E_READ:    return "short read";
        case ELF_E_MAGIC:   return "not an ELF file";
        case ELF_E_CLASS:   return "unsupported ELF class or machine";
        case ELF_E_TYPE:    return "not an executable ELF";
        case ELF_E_PHDR:    return "bad program header table";
        case ELF_E_SEGMENT: return "bad segment";
        case ELF_E_MEM:     return "out of physical memory";
        default:            return "unknown error";
    }
}

static int load_segment(int fd, uint32_t fsize, const elf32_phdr_t* ph) {
    if (ph->p_filesz > ph->p_memsz) return ELF_E_SEGMENT;
    if (ph->p_offset > fsize) return ELF_E_SEGMENT;
    if (ph->p_filesz > fsize - ph->p_offset) return ELF_E_SEGMENT;
    if (ph->p_memsz == 0) return ELF_OK;

    uint32_t start = ph->p_vaddr & ~(uint32_t)(PAGE_SIZE - 1);
    uint32_t end = (ph->p_vaddr + ph->p_memsz + (PAGE_SIZE - 1)) & ~(uint32_t)(PAGE_SIZE - 1);
    if (end <= start) return ELF_E_SEGMENT;

    uint32_t file_end = ph->p_vaddr + ph->p_filesz;

    for (uint32_t va = start; va < end; va += PAGE_SIZE) {
        void* phys = pmm_alloc_block();
        if (!phys) return ELF_E_MEM;

        map_page((uint32_t)phys, va, 0x07);
        memset((void*)va, 0, PAGE_SIZE);

        uint32_t page_end = va + PAGE_SIZE;
        uint32_t ov_start = ph->p_vaddr > va ? ph->p_vaddr : va;
        uint32_t ov_end = file_end < page_end ? file_end : page_end;
        if (ov_end > ov_start) {
            uint32_t file_off = ph->p_offset + (ov_start - ph->p_vaddr);
            uint32_t len = ov_end - ov_start;
            if (vfs_pread(fd, file_off, (void*)ov_start, len) != (long)len) return ELF_E_READ;
        }
    }

    return ELF_OK;
}

int elf_exec(const char* path, uint32_t priority) {
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) return ELF_E_OPEN;

    elf32_ehdr_t eh;
    if (vfs_pread(fd, 0, &eh, sizeof(eh)) != (long)sizeof(eh)) {
        vfs_close(fd);
        return ELF_E_READ;
    }

    if (eh.e_ident[0] != ELFMAG0 || eh.e_ident[1] != ELFMAG1 ||
        eh.e_ident[2] != ELFMAG2 || eh.e_ident[3] != ELFMAG3) {
        vfs_close(fd);
        return ELF_E_MAGIC;
    }

    if (eh.e_ident[4] != ELFCLASS32 || eh.e_ident[5] != ELFDATA2LSB || eh.e_machine != EM_386) {
        vfs_close(fd);
        return ELF_E_CLASS;
    }

    if (eh.e_type != ET_EXEC || eh.e_phnum == 0 || eh.e_phentsize < sizeof(elf32_phdr_t)) {
        vfs_close(fd);
        return ELF_E_TYPE;
    }

    long fsize = vfs_fsize(fd);
    if (fsize < 0) {
        vfs_close(fd);
        return ELF_E_READ;
    }

    for (uint16_t i = 0; i < eh.e_phnum; i++) {
        elf32_phdr_t ph;
        uint32_t off = eh.e_phoff + (uint32_t)i * eh.e_phentsize;
        if (off > (uint32_t)fsize || sizeof(ph) > (uint32_t)fsize - off) {
            vfs_close(fd);
            return ELF_E_PHDR;
        }
        if (vfs_pread(fd, off, &ph, sizeof(ph)) != (long)sizeof(ph)) {
            vfs_close(fd);
            return ELF_E_PHDR;
        }
        if (ph.p_type != PT_LOAD) continue;

        int r = load_segment(fd, (uint32_t)fsize, &ph);
        if (r != ELF_OK) {
            vfs_close(fd);
            return r;
        }
    }

    uint32_t entry = eh.e_entry;
    vfs_close(fd);

    create_user_task((void (*)())entry, priority);
    return ELF_OK;
}
