#include "syscall.h"
#include "task.h"
#include "klog.h"
#include "vfs.h"
#include "elf.h"
#include "string.h"
#include "rtc.h"

static uint32_t rtc_to_epoch(void) {
    int year, month, day, hour, min, sec;
    rtc_get_datetime(&year, &month, &day, &hour, &min, &sec);
    if (year < 100) year += 2000;
    int32_t y = year;
    int32_t m = month;
    int32_t d = day;
    y -= m <= 2;
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = (uint32_t)(y - era * 400);
    uint32_t doy = (153u * (uint32_t)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (uint32_t)d - 1u;
    uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
    int64_t epoch = days * 86400 + hour * 3600 + min * 60 + sec;
    if (epoch < 0) epoch = 0;
    return (uint32_t)epoch;
}

extern int current_task;
extern int open(const char *path, int flags, ...);
extern int close(int fd);
extern long read(int fd, void *buf, size_t count);
extern long write(int fd, const void *buf, size_t count);
extern long lseek(int fd, long offset, int whence);
extern int unlink(const char *path);

uint32_t syscall_dispatch(registers_t *regs) {
    switch (regs->eax) {
        case SYS_EXIT:
            task_exit();
            return 0;

        case SYS_WRITE: {
            const char *buf = (const char *)regs->ebx;
            uint32_t len = regs->ecx;
            if (!buf) return (uint32_t)-1;
            for (uint32_t i = 0; i < len; i++) {
                char c[2] = { buf[i], 0 };
                klog(c);
            }
            return len;
        }

        case SYS_YIELD:
            return 0;

        case SYS_BRK:
            return task_set_heap_break(current_task, regs->ebx);

        case SYS_SPAWN: {
            const char* upath = (const char*)regs->ebx;
            char* const* uargv = (char* const*)regs->ecx;
            int argc = (int)regs->edx;
            if (!upath || argc < 0 || argc > ELF_SPAWN_MAX_ARGV) return (uint32_t)-1;
            if (argc > 0 && !uargv) return (uint32_t)-1;

            char path_buf[ELF_SPAWN_PATH_MAX];
            char arg_bufs[ELF_SPAWN_MAX_ARGV][ELF_SPAWN_ARG_MAX];
            char* argv_k[ELF_SPAWN_MAX_ARGV];

            strncpy(path_buf, upath, ELF_SPAWN_PATH_MAX - 1);
            path_buf[ELF_SPAWN_PATH_MAX - 1] = 0;

            for (int i = 0; i < argc; i++) {
                const char* a = uargv[i];
                if (!a) return (uint32_t)-1;
                strncpy(arg_bufs[i], a, ELF_SPAWN_ARG_MAX - 1);
                arg_bufs[i][ELF_SPAWN_ARG_MAX - 1] = 0;
                argv_k[i] = arg_bufs[i];
            }

            int r = elf_exec_argv(path_buf, argv_k, argc, 1);
            return r == ELF_OK ? 0 : (uint32_t)-1;
        }

        case SYS_GETTIME:
            return rtc_to_epoch();

        case SYS_COSFS_OPEN:
        case SYS_FAT32_OPEN: {
            const char *path = (const char *)regs->ebx;
            int flags = (int)regs->ecx;
            if (!path) return (uint32_t)-1;
            return (uint32_t)open(path, flags);
        }

        case SYS_COSFS_CLOSE:
        case SYS_FAT32_CLOSE:
            return (uint32_t)close((int)regs->ebx);

        case SYS_COSFS_READ:
        case SYS_FAT32_READ: {
            void *buf = (void *)regs->ecx;
            uint32_t count = regs->edx;
            if (!buf) return (uint32_t)-1;
            return (uint32_t)read((int)regs->ebx, buf, count);
        }

        case SYS_COSFS_WRITE:
        case SYS_FAT32_WRITE: {
            const void *buf = (const void *)regs->ecx;
            uint32_t count = regs->edx;
            if (!buf) return (uint32_t)-1;
            return (uint32_t)write((int)regs->ebx, buf, count);
        }

        case SYS_COSFS_LSEEK:
        case SYS_FAT32_LSEEK:
            return (uint32_t)lseek((int)regs->ebx, (long)regs->ecx, (int)regs->edx);

        case SYS_COSFS_UNLINK:
        case SYS_FAT32_UNLINK: {
            const char *path = (const char *)regs->ebx;
            if (!path) return (uint32_t)-1;
            return (uint32_t)unlink(path);
        }

        case SYS_COSFS_MKDIR:
        case SYS_FAT32_MKDIR: {
            const char *path = (const char *)regs->ebx;
            if (!path) return (uint32_t)-1;
            return vfs_mkdir(path) == 0 ? 0 : (uint32_t)-1;
        }

        default:
            return (uint32_t)-1;
    }
}
