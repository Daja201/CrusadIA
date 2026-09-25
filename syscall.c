#include "syscall.h"
#include "task.h"
#include "klog.h"
#include "fs.h"
#include "fat32.h"

extern int open(const char *path, int flags, ...);
extern int close(int fd);
extern long read(int fd, void *buf, size_t count);
extern long write(int fd, const void *buf, size_t count);
extern long lseek(int fd, long offset, int whence);
extern int unlink(const char *path);
extern int fs_create_dir(const char *name, uint32_t parent_inode_idx);
extern uint32_t g_current_dir;

uint32_t syscall_dispatch(registers_t *regs) {
    switch (regs->eax) {
        case SYS_EXIT:
            task_exit();
            return 0;

        case SYS_WRITE: {
            const char *buf = (const char *)regs->ebx;
            uint32_t len = regs->ecx;
            if (!buf) return (uint32_t)-1;
            for (uint32_t i = 0; i < len && buf[i]; i++) {
                char c[2] = { buf[i], 0 };
                klog(c);
            }
            return len;
        }

        case SYS_YIELD:
            return 0;

        case SYS_COSFS_OPEN: {
            const char *path = (const char *)regs->ebx;
            int flags = (int)regs->ecx;
            if (!path) return (uint32_t)-1;
            return (uint32_t)open(path, flags);
        }

        case SYS_COSFS_CLOSE:
            return (uint32_t)close((int)regs->ebx);

        case SYS_COSFS_READ: {
            void *buf = (void *)regs->ecx;
            uint32_t count = regs->edx;
            if (!buf) return (uint32_t)-1;
            return (uint32_t)read((int)regs->ebx, buf, count);
        }

        case SYS_COSFS_WRITE: {
            const void *buf = (const void *)regs->ecx;
            uint32_t count = regs->edx;
            if (!buf) return (uint32_t)-1;
            return (uint32_t)write((int)regs->ebx, buf, count);
        }

        case SYS_COSFS_LSEEK:
            return (uint32_t)lseek((int)regs->ebx, (long)regs->ecx, (int)regs->edx);

        case SYS_COSFS_UNLINK: {
            const char *path = (const char *)regs->ebx;
            if (!path) return (uint32_t)-1;
            return (uint32_t)unlink(path);
        }

        case SYS_COSFS_MKDIR: {
            const char *name = (const char *)regs->ebx;
            if (!name) return (uint32_t)-1;
            return (uint32_t)fs_create_dir(name, g_current_dir);
        }

        case SYS_FAT32_OPEN: {
            const char *path = (const char *)regs->ebx;
            int flags = (int)regs->ecx;
            if (!path) return (uint32_t)-1;
            return (uint32_t)fat32_open(path, flags);
        }

        case SYS_FAT32_CLOSE:
            return (uint32_t)fat32_fclose((int)regs->ebx);

        case SYS_FAT32_READ: {
            void *buf = (void *)regs->ecx;
            uint32_t count = regs->edx;
            if (!buf) return (uint32_t)-1;
            return (uint32_t)fat32_fread((int)regs->ebx, buf, count);
        }

        case SYS_FAT32_WRITE: {
            const void *buf = (const void *)regs->ecx;
            uint32_t count = regs->edx;
            if (!buf) return (uint32_t)-1;
            return (uint32_t)fat32_fwrite((int)regs->ebx, buf, count);
        }

        case SYS_FAT32_LSEEK:
            return (uint32_t)fat32_flseek((int)regs->ebx, (long)regs->ecx, (int)regs->edx);

        case SYS_FAT32_UNLINK: {
            const char *path = (const char *)regs->ebx;
            if (!path) return (uint32_t)-1;
            return (uint32_t)fat32_funlink(path);
        }

        case SYS_FAT32_MKDIR: {
            const char *path = (const char *)regs->ebx;
            if (!path) return (uint32_t)-1;
            return (uint32_t)fat32_fmkdir(path);
        }

        default:
            return (uint32_t)-1;
    }
}
