#include "vfs.h"
#include "fs.h"
#include "fat32.h"
#include "klog.h"
#include "string.h"
#include "heap.h"
#include "fcntl.h"
#include "unistd.h"
#include "sys/stat.h"

extern int g_current_drive;
extern superblock_t g_superblock;
extern char g_current_path[];
extern int current_task;

#define COS_MAGIC          0x5A4C534Au
#define COS_NAME_MAX       27
#define HDR_COS_OFFSET     64
#define HDR_FAT_SECTOR     12
#define HDR_MAGIC          0x56465332u
#define HDR_MAGIC_V1       0x56465331u
#define HDR_MAX_MOUNTS     6
#define VFS_MAX_MOUNTS     (HDR_MAX_MOUNTS + 1)
#define FAT_TMP_MAX        130
#define LOCK_NONE          (-2)

typedef struct {
    uint16_t ata_base;
    uint8_t  is_slave;
    uint8_t  used;
    char     path[VFS_MOUNT_PATH_MAX];
} __attribute__((packed)) vfs_hdr_mount_t;

typedef struct {
    uint32_t magic;
    uint8_t  is_root;
    uint8_t  count;
    uint16_t reserved;
    vfs_hdr_mount_t mounts[HDR_MAX_MOUNTS];
} __attribute__((packed)) vfs_hdr_t;

typedef struct {
    uint16_t ata_base;
    uint8_t  is_slave;
    uint8_t  used;
    char     name[16];
} __attribute__((packed)) vfs_hdr1_mount_t;

typedef struct {
    uint32_t magic;
    uint8_t  is_root;
    uint8_t  count;
    uint16_t reserved;
    vfs_hdr1_mount_t mounts[HDR_MAX_MOUNTS];
} __attribute__((packed)) vfs_hdr1_t;

typedef struct {
    uint32_t inode;
    char     name[28];
} cos_dirent_t;

typedef struct {
    int         used;
    int         drive;
    int         fs;
    int         is_root;
    uint32_t    part_lba;
    uint32_t    seq;
    int         open_count;
    char        mountpoint[VFS_PATH_MAX];
    fat32_fs_t  fat;
} vfs_mount_t;

typedef struct {
    int      used;
    int      mount;
    int      writable;
    int      append;
    uint32_t pos;
    uint32_t size;
    uint32_t id;
    char     rel[VFS_PATH_MAX];
    FILE     stream;
} vfs_file_t;

typedef struct {
    int      is_dir;
    uint32_t size;
    uint32_t id;
} vnode_t;

static vfs_mount_t  s_mounts[VFS_MAX_MOUNTS];
static vfs_mount_t* s_root = 0;
static vfs_file_t   s_files[VFS_MAX_FDS];
static char         s_cwd[VFS_PATH_MAX] = "/";
static int          s_cos_loaded = -1;
static uint32_t     s_seq = 0;
static fat32_dirent_t s_fat_tmp[FAT_TMP_MAX];

static volatile int s_lock = 0;
static volatile int s_owner = LOCK_NONE;
static int          s_depth = 0;

static const char* s_default_dirs[] = { "user", "data", "mount", "lock", "cosfiles" };
#define DEFAULT_DIR_COUNT 5

static void lock_wait(void) {
    uint32_t fl;
    uint32_t cs;
    __asm__ volatile("pushfl; popl %0" : "=r"(fl));
    __asm__ volatile("movl %%cs, %0" : "=r"(cs));
    if ((cs & 3) == 0 && !(fl & 0x200)) {
        __asm__ volatile("sti; pause; cli" : : : "memory");
    } else {
        __asm__ volatile("pause" : : : "memory");
    }
}

static void vfs_lock(void) {
    int me = current_task;
    if (s_depth > 0 && s_owner == me) {
        s_depth++;
        return;
    }
    while (__sync_lock_test_and_set(&s_lock, 1)) lock_wait();
    s_owner = me;
    s_depth = 1;
}

static void vfs_unlock(void) {
    if (--s_depth == 0) {
        s_owner = LOCK_NONE;
        __sync_lock_release(&s_lock);
    }
}

void vfs_task_killed(int task) {
    if (s_depth > 0 && s_owner == task) {
        s_depth = 0;
        s_owner = LOCK_NONE;
        __sync_lock_release(&s_lock);
    }
}

static uint16_t rd16(const uint8_t* p) {
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}

static uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static char lc(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static int ci_prefix(const char* s, const char* p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\0' || lc(s[i]) != lc(p[i])) return 0;
    }
    return 1;
}

static int path_within(const char* path, const char* base) {
    size_t n = strlen(base);
    if (n == 1) return 1;
    if (!ci_prefix(path, base, n)) return 0;
    return path[n] == '\0' || path[n] == '/';
}

static int normalize(const char* in, char* out) {
    char full[VFS_PATH_MAX * 2];
    if (!in || !*in) in = ".";
    if (in[0] == '/') {
        if (strlen(in) >= sizeof(full)) return VFS_E_PATH;
        strcpy(full, in);
    } else {
        if (strlen(s_cwd) + strlen(in) + 2 >= sizeof(full)) return VFS_E_PATH;
        strcpy(full, s_cwd);
        strcat(full, "/");
        strcat(full, in);
    }
    out[0] = '/';
    out[1] = '\0';
    size_t len = 1;
    const char* p = full;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char comp[VFS_PATH_MAX];
        size_t n = 0;
        while (*p && *p != '/') {
            if (n >= VFS_PATH_MAX - 1) return VFS_E_PATH;
            comp[n++] = *p++;
        }
        comp[n] = '\0';
        if (strcmp(comp, ".") == 0) continue;
        if (strcmp(comp, "..") == 0) {
            if (len > 1) {
                while (len > 1 && out[len - 1] != '/') len--;
                if (len > 1) len--;
                out[len] = '\0';
            }
            continue;
        }
        if (len + n + 2 >= VFS_PATH_MAX) return VFS_E_PATH;
        if (len > 1) out[len++] = '/';
        memcpy(out + len, comp, n);
        len += n;
        out[len] = '\0';
    }
    return 0;
}

static int split_rel(const char* rel, char* parent, char* leaf) {
    size_t n = strlen(rel);
    while (n > 1 && rel[n - 1] == '/') n--;
    if (n <= 1) return -1;
    size_t i = n;
    while (i > 0 && rel[i - 1] != '/') i--;
    size_t ll = n - i;
    if (ll == 0 || ll >= VFS_NAME_MAX) return -1;
    memcpy(leaf, rel + i, ll);
    leaf[ll] = '\0';
    if (i <= 1) {
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        memcpy(parent, rel, i - 1);
        parent[i - 1] = '\0';
    }
    return 0;
}

static int fat_name_ok(const char* s) {
    int base = 0, ext = 0, dot = 0;
    if (!*s) return 0;
    for (; *s; s++) {
        if (*s == '.') {
            if (dot || base == 0) return 0;
            dot = 1;
            continue;
        }
        if (*s == ' ' || *s == '/' || *s == '>') return 0;
        if (dot) ext++; else base++;
    }
    return base <= 8 && ext <= 3;
}

static int name_ok(int fs, const char* leaf) {
    if (fs == VFS_FS_FAT32) return fat_name_ok(leaf);
    return strlen(leaf) > 0 && strlen(leaf) <= COS_NAME_MAX;
}

static int is_fat32_bpb(const uint8_t* s) {
    if (s[510] != 0x55 || s[511] != 0xAA) return 0;
    if (s[0] != 0xEB && s[0] != 0xE9) return 0;
    if (rd16(s + 11) != 512) return 0;
    if (s[13] == 0) return 0;
    if (rd32(s + 36) == 0) return 0;
    if (rd32(s + 44) < 2) return 0;
    return 1;
}

static int detect_ex(int drive, uint32_t* part) {
    if (drive < 0 || drive >= g_active_drives) return VFS_FS_NONE;
    fs_device_t* d = &g_drives[drive];
    uint8_t sec[512];
    uint8_t pb[512];
    select_drive(d->ata_base, d->is_slave);
    block_read(0, sec);
    *part = 0;
    if (rd32(sec) == COS_MAGIC) return VFS_FS_COSFS;
    if (is_fat32_bpb(sec)) return VFS_FS_FAT32;
    if (sec[510] == 0x55 && sec[511] == 0xAA) {
        for (int i = 0; i < 4; i++) {
            const uint8_t* e = sec + 446 + 16 * i;
            uint8_t type = e[4];
            uint32_t lba = rd32(e + 8);
            if ((type == 0x0B || type == 0x0C) && lba != 0) {
                block_read(lba, pb);
                if (is_fat32_bpb(pb)) {
                    *part = lba;
                    return VFS_FS_FAT32;
                }
            }
        }
    }
    return VFS_FS_NONE;
}

static int hdr_io(int drive, int fs, uint32_t part, vfs_hdr_t* h, int write) {
    fs_device_t* d = &g_drives[drive];
    uint8_t sec[512];
    uint32_t lba;
    uint32_t off;
    select_drive(d->ata_base, d->is_slave);
    if (fs == VFS_FS_COSFS) {
        lba = 0;
        off = HDR_COS_OFFSET;
    } else if (fs == VFS_FS_FAT32) {
        block_read(part, sec);
        if (rd16(sec + 14) <= HDR_FAT_SECTOR) return VFS_E_NOHDR;
        lba = part + HDR_FAT_SECTOR;
        off = 0;
    } else {
        return VFS_E_NOFS;
    }
    block_read(lba, sec);
    if (write) {
        memcpy(sec + off, h, sizeof(*h));
        block_write(lba, sec);
    } else {
        memcpy(h, sec + off, sizeof(*h));
    }
    return 0;
}

static int hdr_clear(int drive, int fs, uint32_t part) {
    vfs_hdr_t z;
    memset(&z, 0, sizeof(z));
    return hdr_io(drive, fs, part, &z, 1);
}

static vfs_mount_t* find_by_drive(int drive) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (s_mounts[i].used && s_mounts[i].drive == drive) return &s_mounts[i];
    }
    return 0;
}

static vfs_mount_t* find_mount_exact(const char* abs) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (s_mounts[i].used && strcasecmp(s_mounts[i].mountpoint, abs) == 0) return &s_mounts[i];
    }
    return 0;
}

static vfs_mount_t* find_mount(const char* abs, char* rel) {
    vfs_mount_t* best = 0;
    size_t best_len = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        vfs_mount_t* m = &s_mounts[i];
        if (!m->used) continue;
        size_t n = m->is_root ? 1 : strlen(m->mountpoint);
        if (!path_within(abs, m->mountpoint)) continue;
        if (n > best_len) {
            best = m;
            best_len = n;
        }
    }
    if (!best) return 0;
    const char* r = best->is_root ? abs : abs + best_len;
    if (*r == '\0') r = "/";
    strcpy(rel, r);
    return best;
}

static vfs_mount_t* free_slot(void) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!s_mounts[i].used) return &s_mounts[i];
    }
    return 0;
}

static void ctx_enter(vfs_mount_t* m) {
    fs_device_t* d = &g_drives[m->drive];
    select_drive(d->ata_base, d->is_slave);
    if (m->fs == VFS_FS_COSFS) {
        if (s_cos_loaded != m->drive) {
            g_current_drive = m->drive;
            init_fs();
            s_cos_loaded = m->drive;
        }
        g_current_dir = 0;
    } else {
        g_fat32 = m->fat;
    }
}

static void ctx_leave(vfs_mount_t* m) {
    if (m->fs == VFS_FS_FAT32) m->fat = g_fat32;
}

static int cos_resolve(const char* rel) {
    uint32_t cur = 0;
    const char* p = rel;
    char comp[VFS_NAME_MAX];
    for (;;) {
        while (*p == '/') p++;
        if (!*p) return (int)cur;
        size_t n = 0;
        while (*p && *p != '/') {
            if (n < VFS_NAME_MAX - 1) comp[n++] = *p;
            p++;
        }
        comp[n] = '\0';
        inode_t dir;
        read_inode((int)cur, &dir);
        if (dir.type != 2) return -1;
        int next = dir_lookup(&dir, comp);
        if (next < 0) return -1;
        cur = (uint32_t)next;
    }
}

static int fat_parent_cluster(const char* parent_rel, uint32_t* dc) {
    if (strcmp(parent_rel, "/") == 0) {
        *dc = g_fat32.root_cluster;
        return 0;
    }
    fat32_dirent_t d;
    if (fat32_stat(parent_rel, &d) != 0) return VFS_E_NOENT;
    if (!(d.attr & FAT32_ATTR_DIR)) return VFS_E_NOTDIR;
    *dc = d.first_cluster ? d.first_cluster : g_fat32.root_cluster;
    return 0;
}

static int be_stat(vfs_mount_t* m, const char* rel, vnode_t* v) {
    if (m->fs == VFS_FS_COSFS) {
        int idx = cos_resolve(rel);
        if (idx < 0) return VFS_E_NOENT;
        inode_t n;
        read_inode(idx, &n);
        v->is_dir = (n.type == 2);
        v->size = n.size;
        v->id = (uint32_t)idx;
        return 0;
    }
    if (strcmp(rel, "/") == 0) {
        v->is_dir = 1;
        v->size = 0;
        v->id = g_fat32.root_cluster;
        return 0;
    }
    fat32_dirent_t d;
    if (fat32_stat(rel, &d) != 0) return VFS_E_NOENT;
    v->is_dir = (d.attr & FAT32_ATTR_DIR) != 0;
    v->size = d.size;
    v->id = d.first_cluster;
    return 0;
}

static int be_mkdir(vfs_mount_t* m, const char* rel) {
    char parent[VFS_PATH_MAX];
    char leaf[VFS_NAME_MAX];
    if (split_rel(rel, parent, leaf) != 0) return VFS_E_NAME;
    if (!name_ok(m->fs, leaf)) return VFS_E_NAME;
    vnode_t v;
    if (be_stat(m, rel, &v) == 0) return VFS_E_EXIST;
    if (m->fs == VFS_FS_COSFS) {
        int p = cos_resolve(parent);
        if (p < 0) return VFS_E_NOENT;
        inode_t pn;
        read_inode(p, &pn);
        if (pn.type != 2) return VFS_E_NOTDIR;
        if (fs_create_dir(leaf, (uint32_t)p) < 0) return VFS_E_IO;
        if (cos_resolve(rel) < 0) return VFS_E_IO;
        return 0;
    }
    uint32_t dc;
    int r = fat_parent_cluster(parent, &dc);
    if (r) return r;
    if (fat32_create_dir(dc, leaf) != 0) return VFS_E_IO;
    return 0;
}

static int be_create(vfs_mount_t* m, const char* rel, const char* tag) {
    char parent[VFS_PATH_MAX];
    char leaf[VFS_NAME_MAX];
    if (split_rel(rel, parent, leaf) != 0) return VFS_E_NAME;
    if (!name_ok(m->fs, leaf)) return VFS_E_NAME;
    vnode_t v;
    if (be_stat(m, rel, &v) == 0) return VFS_E_EXIST;
    if (m->fs == VFS_FS_COSFS) {
        int p = cos_resolve(parent);
        if (p < 0) return VFS_E_NOENT;
        inode_t pn;
        read_inode(p, &pn);
        if (pn.type != 2) return VFS_E_NOTDIR;
        g_current_dir = (uint32_t)p;
        uint32_t r = fs_create_file(leaf, tag);
        g_current_dir = 0;
        if ((int32_t)r < 0) return VFS_E_IO;
        if (cos_resolve(rel) < 0) return VFS_E_IO;
        return 0;
    }
    uint32_t dc;
    int r = fat_parent_cluster(parent, &dc);
    if (r) return r;
    if (fat32_write_file(dc, leaf, (const uint8_t*)"", 0) != 0) return VFS_E_IO;
    return 0;
}

static int be_remove(vfs_mount_t* m, const char* rel) {
    char parent[VFS_PATH_MAX];
    char leaf[VFS_NAME_MAX];
    if (strcmp(rel, "/") == 0) return VFS_E_BUSY;
    if (split_rel(rel, parent, leaf) != 0) return VFS_E_NAME;
    vnode_t v;
    if (be_stat(m, rel, &v) != 0) return VFS_E_NOENT;
    if (m->fs == VFS_FS_COSFS) {
        int p = cos_resolve(parent);
        if (p < 0) return VFS_E_NOENT;
        if (v.is_dir && !fs_dir_is_empty(v.id)) return VFS_E_NOTEMPTY;
        if (fs_delete_in((uint32_t)p, leaf) < 0) return VFS_E_IO;
        return 0;
    }
    uint32_t dc;
    int r = fat_parent_cluster(parent, &dc);
    if (r) return r;
    if (v.is_dir) {
        int rc = fat32_remove_dir(dc, leaf);
        if (rc == -2) return VFS_E_NOTEMPTY;
        return rc == 0 ? 0 : VFS_E_IO;
    }
    return fat32_delete_file(dc, leaf) == 0 ? 0 : VFS_E_IO;
}

static int be_truncate(vfs_mount_t* m, const char* rel, const char* tag) {
    char parent[VFS_PATH_MAX];
    char leaf[VFS_NAME_MAX];
    if (split_rel(rel, parent, leaf) != 0) return VFS_E_NAME;
    if (m->fs == VFS_FS_COSFS) {
        int idx = cos_resolve(rel);
        int p = cos_resolve(parent);
        if (idx < 0 || p < 0) return VFS_E_NOENT;
        inode_t n;
        read_inode(idx, &n);
        char keep[TAG_LEN + 1];
        memset(keep, 0, sizeof(keep));
        if (n.main_tag[0]) strncpy(keep, n.main_tag, TAG_LEN);
        else strncpy(keep, tag, TAG_LEN);
        if (fs_delete_in((uint32_t)p, leaf) < 0) return VFS_E_IO;
        return be_create(m, rel, keep);
    }
    uint32_t dc;
    int r = fat_parent_cluster(parent, &dc);
    if (r) return r;
    if (fat32_write_file(dc, leaf, (const uint8_t*)"", 0) != 0) return VFS_E_IO;
    return 0;
}

static int be_readdir(vfs_mount_t* m, const char* rel, vfs_dirent_t* out, int max) {
    vnode_t v;
    if (be_stat(m, rel, &v) != 0) return VFS_E_NOENT;
    if (!v.is_dir) return VFS_E_NOTDIR;
    int n = 0;
    if (m->fs == VFS_FS_COSFS) {
        inode_t dir;
        read_inode((int)v.id, &dir);
        uint8_t buf[512];
        int per_block = 512 / (int)sizeof(cos_dirent_t);
        for (int b = 0; b < INODE_DIRECT; b++) {
            uint32_t lba = dir.direct[b];
            if (lba == 0) continue;
            block_read(lba, buf);
            cos_dirent_t* e = (cos_dirent_t*)buf;
            for (int i = 0; i < per_block; i++) {
                if (e[i].inode == 0 || e[i].inode >= g_superblock.inode_count) continue;
                if (n >= max) return n;
                inode_t child;
                read_inode((int)e[i].inode, &child);
                memset(&out[n], 0, sizeof(out[n]));
                strncpy(out[n].name, e[i].name, sizeof(e[i].name));
                out[n].name[sizeof(e[i].name)] = '\0';
                out[n].is_dir = (child.type == 2);
                out[n].size = child.size;
                n++;
            }
        }
        return n;
    }
    int want = max + 2;
    if (want > FAT_TMP_MAX) want = FAT_TMP_MAX;
    int got = fat32_list_dir(v.id, s_fat_tmp, want);
    if (got < 0) return VFS_E_IO;
    for (int i = 0; i < got && n < max; i++) {
        if (strcmp(s_fat_tmp[i].name, ".") == 0 || strcmp(s_fat_tmp[i].name, "..") == 0) continue;
        memset(&out[n], 0, sizeof(out[n]));
        strncpy(out[n].name, s_fat_tmp[i].name, VFS_NAME_MAX - 1);
        out[n].is_dir = (s_fat_tmp[i].attr & FAT32_ATTR_DIR) != 0;
        out[n].size = s_fat_tmp[i].size;
        n++;
    }
    return n;
}

static void prompt_update(void) {
    size_t n = strlen(s_cwd);
    g_current_path[0] = '>';
    if (n <= 60) {
        strcpy(g_current_path + 1, s_cwd);
    } else {
        strcpy(g_current_path + 1, "...");
        strcpy(g_current_path + 4, s_cwd + n - 56);
    }
}

static int ensure_dir_abs(const char* abs) {
    char rel[VFS_PATH_MAX];
    vfs_mount_t* m = find_mount(abs, rel);
    if (!m) return VFS_E_NOROOT;
    ctx_enter(m);
    vnode_t v;
    int r;
    if (be_stat(m, rel, &v) == 0) r = v.is_dir ? 0 : VFS_E_NOTDIR;
    else r = be_mkdir(m, rel);
    ctx_leave(m);
    return r;
}

static int remove_abs(const char* abs) {
    char rel[VFS_PATH_MAX];
    vfs_mount_t* m = find_mount(abs, rel);
    if (!m) return VFS_E_NOROOT;
    ctx_enter(m);
    int r = be_remove(m, rel);
    ctx_leave(m);
    return r;
}

static int drive_refs(int drive, const vfs_mount_t* except) {
    int n = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (s_mounts[i].used && s_mounts[i].drive == drive && &s_mounts[i] != except) n++;
    }
    return n;
}

static vfs_mount_t* find_last_by_drive(int drive) {
    vfs_mount_t* best = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        vfs_mount_t* m = &s_mounts[i];
        if (!m->used || m->drive != drive) continue;
        if (!best || m->seq > best->seq) best = m;
    }
    return best;
}

static int target_ok(const char* t) {
    size_t n = strlen(t);
    return n > 1 && n < VFS_MOUNT_PATH_MAX && t[0] == '/';
}

static int persist(void) {
    if (!s_root) return VFS_E_NOROOT;
    vfs_mount_t* list[VFS_MAX_MOUNTS];
    int cnt = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        vfs_mount_t* m = &s_mounts[i];
        if (!m->used || m->is_root || !target_ok(m->mountpoint)) continue;
        list[cnt++] = m;
    }
    for (int a = 0; a < cnt; a++) {
        for (int b = a + 1; b < cnt; b++) {
            if (list[b]->seq < list[a]->seq) {
                vfs_mount_t* t = list[a];
                list[a] = list[b];
                list[b] = t;
            }
        }
    }
    vfs_hdr_t h;
    memset(&h, 0, sizeof(h));
    h.magic = HDR_MAGIC;
    h.is_root = 1;
    for (int a = 0; a < cnt && h.count < HDR_MAX_MOUNTS; a++) {
        fs_device_t* d = &g_drives[list[a]->drive];
        vfs_hdr_mount_t* e = &h.mounts[h.count];
        e->ata_base = d->ata_base;
        e->is_slave = d->is_slave;
        e->used = 1;
        strcpy(e->path, list[a]->mountpoint);
        h.count++;
    }
    return hdr_io(s_root->drive, s_root->fs, s_root->part_lba, &h, 1);
}

static int mount_backend(vfs_mount_t* m, int drive, int fs, uint32_t part) {
    fs_device_t* d = &g_drives[drive];
    memset(m, 0, sizeof(*m));
    m->drive = drive;
    m->fs = fs;
    m->part_lba = part;
    select_drive(d->ata_base, d->is_slave);
    if (fs == VFS_FS_COSFS) {
        s_cos_loaded = -1;
        g_current_drive = drive;
        init_fs();
        if (g_superblock.magic != COS_MAGIC) return VFS_E_IO;
        s_cos_loaded = drive;
        return 0;
    }
    if (fat32_mount(part) != 0) return VFS_E_IO;
    m->fat = g_fat32;
    return 0;
}

static void backend_unmount(vfs_mount_t* m) {
    if (drive_refs(m->drive, m) > 0) return;
    if (m->fs == VFS_FS_COSFS) {
        if (s_cos_loaded == m->drive) s_cos_loaded = -1;
        return;
    }
    ctx_enter(m);
    fat32_unmount();
}

static void set_cwd_default(void) {
    char rel[VFS_PATH_MAX];
    strcpy(s_cwd, "/");
    vfs_mount_t* m = find_mount("/user", rel);
    if (m) {
        ctx_enter(m);
        vnode_t v;
        if (be_stat(m, rel, &v) == 0 && v.is_dir) strcpy(s_cwd, "/user");
        ctx_leave(m);
    }
    prompt_update();
}

static int do_mount(int drive, const char* t, int boot) {
    int is_root = (strcmp(t, "/") == 0);
    if (is_root) {
        if (s_root) return VFS_E_BUSY;
    } else {
        if (!s_root) return VFS_E_NOROOT;
        if (!target_ok(t)) return VFS_E_TARGET;
        if (find_mount_exact(t)) return VFS_E_BUSY;
    }
    uint32_t part = 0;
    int fs = detect_ex(drive, &part);
    if (fs == VFS_FS_NONE) return VFS_E_NOFS;
    vfs_mount_t* slot = free_slot();
    if (!slot) return VFS_E_BUSY;
    int r;
    if (!is_root) {
        if (path_within(t, "/mount")) {
            r = ensure_dir_abs("/mount");
            if (r) return r;
        }
        r = ensure_dir_abs(t);
        if (r) return r;
    }
    r = mount_backend(slot, drive, fs, part);
    if (r) return r;
    strcpy(slot->mountpoint, t);
    slot->used = 1;
    slot->seq = ++s_seq;
    if (is_root) {
        slot->is_root = 1;
        s_root = slot;
        if (!boot) {
            for (int i = 0; i < DEFAULT_DIR_COUNT; i++) {
                char p[VFS_PATH_MAX];
                strcpy(p, "/");
                strcat(p, s_default_dirs[i]);
                ensure_dir_abs(p);
            }
            r = persist();
            if (r) {
                backend_unmount(slot);
                slot->used = 0;
                s_root = 0;
                return r;
            }
        }
        set_cwd_default();
    } else if (!boot) {
        persist();
    }
    return 0;
}

static int cwd_fix_after_unmount(const char* mp) {
    if (path_within(s_cwd, mp)) {
        strcpy(s_cwd, "/");
        prompt_update();
        return 1;
    }
    return 0;
}

static int do_umount(const char* what) {
    vfs_mount_t* m = 0;
    char abs[VFS_PATH_MAX];
    if (!what || !*what) return VFS_E_NOTMOUNTED;
    int d = (what[0] != '/') ? vfs_parse_drive(what) : -1;
    if (d >= 0) {
        m = find_last_by_drive(d);
    } else {
        int r = normalize(what, abs);
        if (r) return r;
        m = find_mount_exact(abs);
    }
    if (!m) return VFS_E_NOTMOUNTED;
    int idx = (int)(m - s_mounts);
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (i == idx || !s_mounts[i].used) continue;
        if (m->is_root) return VFS_E_BUSY;
        if (!s_mounts[i].is_root && path_within(s_mounts[i].mountpoint, m->mountpoint)) return VFS_E_BUSY;
    }
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (s_files[i].used && s_files[i].mount == idx) return VFS_E_BUSY;
    }
    char mp[VFS_PATH_MAX];
    strcpy(mp, m->mountpoint);
    int drive = m->drive;
    int fs = m->fs;
    uint32_t part = m->part_lba;
    int was_root = m->is_root;
    backend_unmount(m);
    m->used = 0;
    if (was_root) {
        s_root = 0;
        if (drive_refs(drive, 0) == 0) hdr_clear(drive, fs, part);
        strcpy(s_cwd, "/");
        prompt_update();
    } else {
        persist();
        cwd_fix_after_unmount(mp);
    }
    return 0;
}

static int do_format(int drive, int fs) {
    fs_device_t* d = &g_drives[drive];
    select_drive(d->ata_base, d->is_slave);
    if (fs == VFS_FS_COSFS) {
        if (d->total_sectors < 1024) return VFS_E_TOOSMALL;
        s_cos_loaded = -1;
        g_current_drive = drive;
        qformat_fs();
        g_current_dir = 0;
        s_cos_loaded = (g_superblock.magic == COS_MAGIC) ? drive : -1;
        return s_cos_loaded < 0 ? VFS_E_IO : 0;
    }
    if (d->total_sectors == 0) return VFS_E_NODRIVE;
    if (fat32_format(0, d->total_sectors, 0) != 0) return VFS_E_TOOSMALL;
    for (int i = 0; i < DEFAULT_DIR_COUNT; i++) {
        fat32_create_dir(g_fat32.root_cluster, s_default_dirs[i]);
    }
    fat32_unmount();
    hdr_clear(drive, VFS_FS_FAT32, 0);
    return 0;
}

static long read_at(vfs_mount_t* m, vfs_file_t* f, uint32_t off, void* buf, uint32_t n) {
    if (m->fs == VFS_FS_COSFS) {
        inode_t node;
        read_inode((int)f->id, &node);
        f->size = node.size;
        return (long)fs_read(f->id, &node, off, n, (uint8_t*)buf);
    }
    fat32_dirent_t d;
    memset(&d, 0, sizeof(d));
    d.first_cluster = f->id;
    d.size = f->size;
    return (long)fat32_read(&d, off, n, (uint8_t*)buf);
}

static long fat_write_at(vfs_file_t* f, uint32_t pos, const uint8_t* data, uint32_t len) {
    char parent[VFS_PATH_MAX];
    char leaf[VFS_NAME_MAX];
    uint32_t dc;
    if (len == 0) return 0;
    if (split_rel(f->rel, parent, leaf) != 0) return VFS_E_NAME;
    if (fat_parent_cluster(parent, &dc) != 0) return VFS_E_IO;
    if (pos == f->size) {
        if (fat32_append_file(dc, leaf, data, len) != 0) return VFS_E_IO;
    } else {
        uint32_t end = pos + len;
        uint32_t total = end > f->size ? end : f->size;
        uint8_t* buf = (uint8_t*)malloc(total);
        if (!buf) return VFS_E_IO;
        memset(buf, 0, total);
        if (f->size) {
            fat32_dirent_t d;
            memset(&d, 0, sizeof(d));
            d.first_cluster = f->id;
            d.size = f->size;
            if (fat32_read(&d, 0, f->size, buf) != f->size) {
                free(buf);
                return VFS_E_IO;
            }
        }
        memcpy(buf + pos, data, len);
        int rc = fat32_write_file(dc, leaf, buf, total);
        free(buf);
        if (rc != 0) return VFS_E_IO;
    }
    fat32_dirent_t fresh;
    if (fat32_stat(f->rel, &fresh) != 0) return VFS_E_IO;
    f->id = fresh.first_cluster;
    f->size = fresh.size;
    return (long)len;
}

static long cos_write_at(vfs_file_t* f, uint32_t pos, const uint8_t* data, uint32_t len) {
    if (len == 0) return 0;
    int w = fs_write(f->id, pos, data, len);
    if (w < 0) return VFS_E_IO;
    inode_t n;
    read_inode((int)f->id, &n);
    f->size = n.size;
    return (long)w;
}

static vfs_file_t* fget(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !s_files[fd].used) return 0;
    return &s_files[fd];
}

static int open_locked(const char* path, int flags, const char* tag) {
    char abs[VFS_PATH_MAX];
    char rel[VFS_PATH_MAX];
    int r = normalize(path, abs);
    if (r) return r;
    vfs_mount_t* m = find_mount(abs, rel);
    if (!m) return VFS_E_NOROOT;
    int slot = -1;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (!s_files[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return VFS_E_MFILE;
    int writable = (flags & 3) != 0;
    vnode_t v;
    ctx_enter(m);
    int exists = (be_stat(m, rel, &v) == 0);
    r = 0;
    if (exists && v.is_dir) {
        r = VFS_E_ISDIR;
    } else if (!exists) {
        if (flags & O_CREAT) {
            r = be_create(m, rel, tag);
            if (!r) r = be_stat(m, rel, &v);
        } else {
            r = VFS_E_NOENT;
        }
    } else if ((flags & O_CREAT) && (flags & O_EXCL)) {
        r = VFS_E_EXIST;
    } else if ((flags & O_TRUNC) && writable) {
        r = be_truncate(m, rel, tag);
        if (!r) r = be_stat(m, rel, &v);
    }
    ctx_leave(m);
    if (r) return r;
    vfs_file_t* f = &s_files[slot];
    memset(f, 0, sizeof(*f));
    f->used = 1;
    f->mount = (int)(m - s_mounts);
    f->writable = writable;
    f->append = (flags & O_APPEND) != 0;
    f->size = v.size;
    f->id = v.id;
    f->pos = f->append ? f->size : 0;
    strcpy(f->rel, rel);
    f->stream.eof = 0;
    m->open_count++;
    return slot;
}

int vfs_drive_count(void) {
    return g_active_drives;
}

uint32_t vfs_drive_size_mb(int drive) {
    if (drive < 0 || drive >= g_active_drives) return 0;
    return g_drives[drive].total_sectors / 2048;
}

int vfs_parse_drive(const char* s) {
    if (!s || !*s) return -1;
    if (s[0] == 'd' || s[0] == 'D') s++;
    int v = 0;
    int digits = 0;
    for (; *s && *s != '.'; s++) {
        if (*s < '0' || *s > '9') return -1;
        v = v * 10 + (*s - '0');
        digits++;
        if (v > 99) return -1;
    }
    if (!digits) return -1;
    if (*s == '.') {
        s++;
        if (s[0] != '0' || s[1] != '\0') return -1;
    }
    if (v >= g_active_drives) return -1;
    return v;
}

int vfs_detect(int drive) {
    vfs_lock();
    uint32_t part;
    int fs = detect_ex(drive, &part);
    vfs_unlock();
    return fs;
}

const char* vfs_fs_name(int fs) {
    if (fs == VFS_FS_COSFS) return "cosfs";
    if (fs == VFS_FS_FAT32) return "fat32";
    return "unknown";
}

const char* vfs_strerror(int err) {
    switch (err) {
        case VFS_OK:            return "ok";
        case VFS_E_NODRIVE:     return "no such drive";
        case VFS_E_NOFS:        return "no known filesystem on drive (use format)";
        case VFS_E_MOUNTED:     return "drive is mounted (unmount it first)";
        case VFS_E_NOROOT:      return "no root drive mounted (mount <drive> / first)";
        case VFS_E_TARGET:      return "bad mount target (use / or an absolute path shorter than 48 characters)";
        case VFS_E_BUSY:        return "busy or already in use";
        case VFS_E_NOTMOUNTED:  return "not mounted";
        case VFS_E_IO:          return "i/o error";
        case VFS_E_FSNAME:      return "unknown filesystem (use fat32 or cos)";
        case VFS_E_TOOSMALL:    return "drive too small for this filesystem";
        case VFS_E_NOHDR:       return "drive has no room for the vfs header";
        case VFS_E_NAME:        return "invalid or too long name";
        case VFS_E_NOENT:       return "no such file or directory";
        case VFS_E_EXIST:       return "already exists";
        case VFS_E_NOTDIR:      return "not a directory";
        case VFS_E_ISDIR:       return "is a directory";
        case VFS_E_PATH:        return "invalid path";
        case VFS_E_NOTEMPTY:    return "directory not empty";
        case VFS_E_MFILE:       return "too many open files";
        case VFS_E_BADF:        return "bad file descriptor";
        case VFS_E_NOTSUP:      return "not supported on this filesystem";
        default:                return "unknown error";
    }
}

const char* vfs_drive_mountpoint(int drive) {
    vfs_mount_t* m = find_by_drive(drive);
    return m ? m->mountpoint : 0;
}

int vfs_mount(const char* drive, const char* target) {
    int d = vfs_parse_drive(drive);
    if (d < 0) return VFS_E_NODRIVE;
    vfs_lock();
    char t[VFS_PATH_MAX];
    int r = normalize(target, t);
    if (!r) r = do_mount(d, t, 0);
    vfs_unlock();
    return r;
}

int vfs_umount(const char* what) {
    vfs_lock();
    int r = do_umount(what);
    vfs_unlock();
    return r;
}

int vfs_format(const char* drive, const char* fsname) {
    int d = vfs_parse_drive(drive);
    if (d < 0) return VFS_E_NODRIVE;
    int fs;
    if (!fsname) return VFS_E_FSNAME;
    if (strcasecmp(fsname, "cosfs") == 0 || strcasecmp(fsname, "cos") == 0) fs = VFS_FS_COSFS;
    else if (strcasecmp(fsname, "fat32") == 0 || strcasecmp(fsname, "fat") == 0) fs = VFS_FS_FAT32;
    else return VFS_E_FSNAME;
    vfs_lock();
    int r;
    if (drive_refs(d, 0) > 0) r = VFS_E_MOUNTED;
    else r = do_format(d, fs);
    vfs_unlock();
    return r;
}

int vfs_mount_count(void) {
    vfs_lock();
    int n = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) if (s_mounts[i].used) n++;
    vfs_unlock();
    return n;
}

int vfs_mount_get(int index, vfs_mount_info_t* out) {
    vfs_lock();
    int r = -1;
    int n = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!s_mounts[i].used) continue;
        if (n == index) {
            out->drive = s_mounts[i].drive;
            out->fs = s_mounts[i].fs;
            out->is_root = s_mounts[i].is_root;
            strcpy(out->mountpoint, s_mounts[i].mountpoint);
            r = 0;
            break;
        }
        n++;
    }
    vfs_unlock();
    return r;
}

const char* vfs_getcwd(void) {
    return s_cwd;
}

int vfs_chdir(const char* path) {
    vfs_lock();
    char abs[VFS_PATH_MAX];
    char rel[VFS_PATH_MAX];
    int r = normalize(path, abs);
    if (!r) {
        vfs_mount_t* m = find_mount(abs, rel);
        if (!m) {
            r = VFS_E_NOROOT;
        } else {
            vnode_t v;
            ctx_enter(m);
            r = be_stat(m, rel, &v);
            ctx_leave(m);
            if (!r && !v.is_dir) r = VFS_E_NOTDIR;
            if (!r) {
                strcpy(s_cwd, abs);
                prompt_update();
            }
        }
    }
    vfs_unlock();
    return r;
}

int vfs_stat(const char* path, vfs_stat_t* out) {
    vfs_lock();
    char abs[VFS_PATH_MAX];
    char rel[VFS_PATH_MAX];
    int r = normalize(path, abs);
    if (!r) {
        vfs_mount_t* m = find_mount(abs, rel);
        if (!m) {
            r = VFS_E_NOROOT;
        } else {
            vnode_t v;
            ctx_enter(m);
            r = be_stat(m, rel, &v);
            ctx_leave(m);
            if (!r) {
                out->is_dir = (uint8_t)v.is_dir;
                out->size = v.size;
            }
        }
    }
    vfs_unlock();
    return r;
}

int vfs_readdir(const char* path, vfs_dirent_t* out, int max) {
    vfs_lock();
    char abs[VFS_PATH_MAX];
    char rel[VFS_PATH_MAX];
    int r = normalize(path, abs);
    if (!r) {
        vfs_mount_t* m = find_mount(abs, rel);
        if (!m) {
            r = VFS_E_NOROOT;
        } else {
            ctx_enter(m);
            r = be_readdir(m, rel, out, max);
            ctx_leave(m);
        }
    }
    vfs_unlock();
    return r;
}

int vfs_mkdir(const char* path) {
    vfs_lock();
    char abs[VFS_PATH_MAX];
    char rel[VFS_PATH_MAX];
    int r = normalize(path, abs);
    if (!r) {
        vfs_mount_t* m = find_mount(abs, rel);
        if (!m) {
            r = VFS_E_NOROOT;
        } else if (find_mount_exact(abs)) {
            r = VFS_E_EXIST;
        } else {
            ctx_enter(m);
            r = be_mkdir(m, rel);
            ctx_leave(m);
        }
    }
    vfs_unlock();
    return r;
}

int vfs_remove(const char* path) {
    vfs_lock();
    char abs[VFS_PATH_MAX];
    int r = normalize(path, abs);
    if (!r) {
        if (find_mount_exact(abs)) r = VFS_E_BUSY;
        else r = remove_abs(abs);
    }
    vfs_unlock();
    return r;
}

int vfs_open(const char* path, int flags) {
    vfs_lock();
    int r = open_locked(path, flags, "file");
    vfs_unlock();
    return r;
}

static int vfs_open_tcc(const char* path, int flags) {
    vfs_lock();
    int r = open_locked(path, flags, "tcc");
    vfs_unlock();
    return r;
}

int vfs_close(int fd) {
    vfs_lock();
    vfs_file_t* f = fget(fd);
    int r = VFS_E_BADF;
    if (f) {
        s_mounts[f->mount].open_count--;
        f->used = 0;
        r = 0;
    }
    vfs_unlock();
    return r;
}

long vfs_read(int fd, void* buf, uint32_t count) {
    vfs_lock();
    vfs_file_t* f = fget(fd);
    long r = -1;
    if (f && buf) {
        vfs_mount_t* m = &s_mounts[f->mount];
        ctx_enter(m);
        r = read_at(m, f, f->pos, buf, count);
        ctx_leave(m);
        if (r > 0) f->pos += (uint32_t)r;
    }
    vfs_unlock();
    return r;
}

long vfs_pread(int fd, uint32_t offset, void* buf, uint32_t count) {
    vfs_lock();
    vfs_file_t* f = fget(fd);
    long r = -1;
    if (f && buf) {
        vfs_mount_t* m = &s_mounts[f->mount];
        ctx_enter(m);
        r = read_at(m, f, offset, buf, count);
        ctx_leave(m);
    }
    vfs_unlock();
    return r;
}

long vfs_write(int fd, const void* buf, uint32_t count) {
    vfs_lock();
    vfs_file_t* f = fget(fd);
    long r = -1;
    if (f && f->writable && buf) {
        vfs_mount_t* m = &s_mounts[f->mount];
        if (f->append) f->pos = f->size;
        ctx_enter(m);
        if (m->fs == VFS_FS_COSFS) r = cos_write_at(f, f->pos, (const uint8_t*)buf, count);
        else r = fat_write_at(f, f->pos, (const uint8_t*)buf, count);
        ctx_leave(m);
        if (r > 0) f->pos += (uint32_t)r;
        if (r < 0) r = -1;
    }
    vfs_unlock();
    return r;
}

long vfs_lseek(int fd, long offset, int whence) {
    vfs_lock();
    vfs_file_t* f = fget(fd);
    long r = -1;
    if (f) {
        long base;
        if (whence == SEEK_SET) base = 0;
        else if (whence == SEEK_CUR) base = (long)f->pos;
        else if (whence == SEEK_END) base = (long)f->size;
        else base = -1;
        if (base >= 0 && base + offset >= 0) {
            f->pos = (uint32_t)(base + offset);
            r = (long)f->pos;
        }
    }
    vfs_unlock();
    return r;
}

long vfs_fsize(int fd) {
    vfs_lock();
    vfs_file_t* f = fget(fd);
    long r = f ? (long)f->size : -1;
    vfs_unlock();
    return r;
}

long vfs_write_file(const char* path, const void* data, uint32_t len, const char* tag, int append) {
    vfs_lock();
    int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
    int fd = open_locked(path, flags, (tag && *tag) ? tag : "file");
    long w = 0;
    if (fd >= 0) {
        if (len > 0) w = vfs_write(fd, data, len);
        vfs_close(fd);
    } else {
        w = fd;
    }
    vfs_unlock();
    if (fd < 0) return fd;
    return w < 0 ? VFS_E_IO : w;
}

int vfs_find_by_tag(const char* tag, uint32_t* results, int max_results) {
    vfs_lock();
    char rel[VFS_PATH_MAX];
    int r;
    vfs_mount_t* m = find_mount(s_cwd, rel);
    if (!m) {
        r = VFS_E_NOROOT;
    } else if (m->fs != VFS_FS_COSFS) {
        r = VFS_E_NOTSUP;
    } else {
        ctx_enter(m);
        r = fs_find_by_tag(tag, results, max_results);
    }
    vfs_unlock();
    return r;
}

void vfs_sync(void) {
    vfs_lock();
    if (s_cos_loaded >= 0) {
        fs_device_t* d = &g_drives[s_cos_loaded];
        select_drive(d->ata_base, d->is_slave);
        save_block_bitmap();
        save_inode_bitmap();
    }
    vfs_unlock();
}

void vfs_shutdown(void) {
    vfs_lock();
    vfs_sync();
    for (int i = VFS_MAX_MOUNTS - 1; i >= 0; i--) {
        vfs_mount_t* m = &s_mounts[i];
        if (!m->used) continue;
        backend_unmount(m);
        m->used = 0;
    }
    s_root = 0;
    vfs_unlock();
}

void vfs_init(void) {
    vfs_lock();
    memset(s_mounts, 0, sizeof(s_mounts));
    memset(s_files, 0, sizeof(s_files));
    s_root = 0;
    s_cos_loaded = -1;
    s_seq = 0;
    strcpy(s_cwd, "/");
    prompt_update();

    int root_drive = -1;
    vfs_hdr_t hdr;
    for (int i = 0; i < g_active_drives; i++) {
        uint32_t part;
        int fs = detect_ex(i, &part);
        if (fs == VFS_FS_NONE) continue;
        if (hdr_io(i, fs, part, &hdr, 0) != 0) continue;
        if (hdr.magic == HDR_MAGIC_V1) {
            vfs_hdr1_t old;
            memcpy(&old, &hdr, sizeof(old));
            memset(&hdr, 0, sizeof(hdr));
            hdr.magic = HDR_MAGIC;
            hdr.is_root = old.is_root;
            hdr.count = old.count;
            for (int k = 0; k < HDR_MAX_MOUNTS; k++) {
                hdr.mounts[k].ata_base = old.mounts[k].ata_base;
                hdr.mounts[k].is_slave = old.mounts[k].is_slave;
                hdr.mounts[k].used = old.mounts[k].used;
                strcpy(hdr.mounts[k].path, "/mount/");
                size_t n = strlen(hdr.mounts[k].path);
                for (int c = 0; c < 15 && old.mounts[k].name[c]; c++) hdr.mounts[k].path[n++] = old.mounts[k].name[c];
                hdr.mounts[k].path[n] = '\0';
            }
        }
        if (hdr.magic == HDR_MAGIC && hdr.is_root) {
            root_drive = i;
            break;
        }
    }
    if (root_drive < 0) {
        klog_status("NO ROOT DRIVE: use mount <drive> /", 0xFFFF00);
        vfs_unlock();
        return;
    }
    if (do_mount(root_drive, "/", 1) != 0) {
        klog_status("ROOT MOUNT FAILED", 0xFF0000);
        vfs_unlock();
        return;
    }
    for (int k = 0; k < hdr.count && k < HDR_MAX_MOUNTS; k++) {
        if (!hdr.mounts[k].used) continue;
        int target = -1;
        for (int i = 0; i < g_active_drives; i++) {
            if (g_drives[i].ata_base == hdr.mounts[k].ata_base &&
                g_drives[i].is_slave == hdr.mounts[k].is_slave) {
                target = i;
                break;
            }
        }
        if (target < 0) continue;
        char t[VFS_PATH_MAX];
        memcpy(t, hdr.mounts[k].path, VFS_MOUNT_PATH_MAX);
        t[VFS_MOUNT_PATH_MAX - 1] = '\0';
        if (t[0] != '/') continue;
        do_mount(target, t, 1);
    }
    set_cwd_default();
    vfs_unlock();
}

int open(const char* path, int flags, ...) {
    int r = vfs_open_tcc(path, flags);
    return r < 0 ? -1 : r;
}

int close(int fd) {
    return vfs_close(fd) < 0 ? -1 : 0;
}

long read(int fd, void* buf, size_t count) {
    return vfs_read(fd, buf, (uint32_t)count);
}

long write(int fd, const void* buf, size_t count) {
    return vfs_write(fd, buf, (uint32_t)count);
}

long lseek(int fd, long offset, int whence) {
    return vfs_lseek(fd, offset, whence);
}

int unlink(const char* path) {
    vfs_stat_t st;
    if (vfs_stat(path, &st) < 0 || st.is_dir) return -1;
    return vfs_remove(path) < 0 ? -1 : 0;
}

char* getcwd(char* buf, size_t size) {
    if (!buf || strlen(s_cwd) + 1 > size) return 0;
    strcpy(buf, s_cwd);
    return buf;
}

int fstat(int fd, struct stat* st) {
    long sz = vfs_fsize(fd);
    if (sz < 0 || !st) return -1;
    st->st_size = (uint32_t)sz;
    st->st_mode = S_IFREG;
    return 0;
}

int stat(const char* path, struct stat* st) {
    vfs_stat_t vs;
    if (!st || vfs_stat(path, &vs) < 0) return -1;
    st->st_size = vs.size;
    st->st_mode = vs.is_dir ? S_IFDIR : S_IFREG;
    return 0;
}

static int stream_fd(FILE* f) {
    if (!f) return -1;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (f == &s_files[i].stream && s_files[i].used) return i;
    }
    return -1;
}

FILE* fopen(const char* path, const char* mode) {
    int plus = strchr(mode, '+') != 0;
    int flags;
    if (mode[0] == 'w') flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a') flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
    else flags = plus ? O_RDWR : O_RDONLY;
    int fd = vfs_open_tcc(path, flags);
    if (fd < 0) return 0;
    return &s_files[fd].stream;
}

FILE* fdopen(int fd, const char* mode) {
    (void)mode;
    if (!fget(fd)) return 0;
    return &s_files[fd].stream;
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* f) {
    int fd = stream_fd(f);
    if (fd < 0 || size == 0 || nmemb == 0) return 0;
    uint32_t want = (uint32_t)(size * nmemb);
    long got = vfs_read(fd, ptr, want);
    if (got <= 0) {
        f->eof = 1;
        return 0;
    }
    if ((uint32_t)got < want) f->eof = 1;
    return (size_t)got / size;
}

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* f) {
    int fd = stream_fd(f);
    if (fd < 0 || size == 0 || nmemb == 0) return 0;
    long w = vfs_write(fd, ptr, (uint32_t)(size * nmemb));
    if (w < 0) return 0;
    return (size_t)w / size;
}

int fclose(FILE* f) {
    int fd = stream_fd(f);
    if (fd < 0) return -1;
    return vfs_close(fd) < 0 ? -1 : 0;
}

int fseek(FILE* f, long offset, int whence) {
    int fd = stream_fd(f);
    if (fd < 0) return -1;
    if (vfs_lseek(fd, offset, whence) < 0) return -1;
    f->eof = 0;
    return 0;
}

long ftell(FILE* f) {
    int fd = stream_fd(f);
    if (fd < 0) return -1;
    return vfs_lseek(fd, 0, SEEK_CUR);
}

int feof(FILE* f) {
    if (!f) return 1;
    return f->eof;
}