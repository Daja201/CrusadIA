#ifndef VFS_H
#define VFS_H
#include <stdint.h>
#include <stddef.h>

#define VFS_PATH_MAX   128
#define VFS_NAME_MAX   32
#define VFS_MAX_FDS    32
#define VFS_MOUNT_PATH_MAX 48

#define VFS_FS_NONE    0
#define VFS_FS_COSFS   1
#define VFS_FS_FAT32   2

#define VFS_OK             0
#define VFS_E_NODRIVE     -1
#define VFS_E_NOFS        -2
#define VFS_E_MOUNTED     -3
#define VFS_E_NOROOT      -4
#define VFS_E_TARGET      -5
#define VFS_E_BUSY        -6
#define VFS_E_NOTMOUNTED  -7
#define VFS_E_IO          -8
#define VFS_E_FSNAME      -9
#define VFS_E_TOOSMALL   -10
#define VFS_E_NOHDR      -11
#define VFS_E_NAME       -12
#define VFS_E_NOENT      -13
#define VFS_E_EXIST      -14
#define VFS_E_NOTDIR     -15
#define VFS_E_ISDIR      -16
#define VFS_E_PATH       -17
#define VFS_E_NOTEMPTY   -18
#define VFS_E_MFILE      -19
#define VFS_E_BADF       -20
#define VFS_E_NOTSUP     -21

typedef struct {
    char     name[VFS_NAME_MAX];
    uint8_t  is_dir;
    uint32_t size;
} vfs_dirent_t;

typedef struct {
    uint8_t  is_dir;
    uint32_t size;
} vfs_stat_t;

typedef struct {
    int  drive;
    int  fs;
    int  is_root;
    char mountpoint[VFS_PATH_MAX];
} vfs_mount_info_t;

void        vfs_init(void);
void        vfs_sync(void);
void        vfs_shutdown(void);
void        vfs_task_killed(int task);

int         vfs_drive_count(void);
uint32_t    vfs_drive_size_mb(int drive);
int         vfs_parse_drive(const char* s);
int         vfs_detect(int drive);
const char* vfs_fs_name(int fs);
const char* vfs_strerror(int err);
const char* vfs_drive_mountpoint(int drive);

int         vfs_mount(const char* drive, const char* target);
int         vfs_umount(const char* what);
int         vfs_format(const char* drive, const char* fsname);
int         vfs_mount_count(void);
int         vfs_mount_get(int index, vfs_mount_info_t* out);

const char* vfs_getcwd(void);
int         vfs_chdir(const char* path);
int         vfs_stat(const char* path, vfs_stat_t* out);
int         vfs_readdir(const char* path, vfs_dirent_t* out, int max);
int         vfs_mkdir(const char* path);
int         vfs_remove(const char* path);

int         vfs_open(const char* path, int flags);
int         vfs_close(int fd);
long        vfs_read(int fd, void* buf, uint32_t count);
long        vfs_write(int fd, const void* buf, uint32_t count);
long        vfs_lseek(int fd, long offset, int whence);
long        vfs_pread(int fd, uint32_t offset, void* buf, uint32_t count);
long        vfs_fsize(int fd);
long        vfs_write_file(const char* path, const void* data, uint32_t len, const char* tag, int append);

int         vfs_find_by_tag(const char* tag, uint32_t* results, int max_results);

#endif
