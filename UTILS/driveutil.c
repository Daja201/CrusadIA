/*
 * Build:   gcc -O2 -Wall -o cosutil cosutil.c
 *
 * Usage:   cosutil <disk.img> <command> [args...]
 *
 * Commands:
 *   info                                show superblock / layout info
 *   ls   [path]                         list a directory (default: root)
 *   tree                                recursive directory listing
 *   cat  <path>                         dump file contents to stdout
 *   get  <path> <local_out_file>        extract a file to the host fs
 *   put  <local_in_file> <path> [tag]   create a file from a host file
 *   mkdir <path>                        create a directory
 *   rm   <path>                         delete a file (files only)
 *   tags <tag>                          list inodes whose main_tag or
 *                                       tags[] match <tag>
 *   mkfs [total_sectors] [inode_count]  create a fresh, empty COS image
*/

#include <stdlib.h>
#include "string.h"
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define INODE_DIRECT 12
#define MAX_TAGS 3
#define TAG_LEN 12
#define BLOCK_BITMAP_MAX_SIZE 32768
#define INODE_BITMAP_SIZE 32768

#define SECTOR_SIZE 512
#define INODE_SIZE 128
#define INODES_PER_BLOCK (SECTOR_SIZE / INODE_SIZE)
#define PTRS_PER_BLOCK (SECTOR_SIZE / sizeof(uint32_t))
#define SUPERBLOCK_LBA 0
#define ROOT_INODE 0
#define FS_MAGIC 0x5A4C534AU

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t block_size;
    uint64_t total_blocks;
    uint32_t inode_count;
    uint32_t bitmap_start;
    uint32_t inode_start;
    uint32_t data_start;
} superblock_t;

typedef struct {
    uint32_t size;
    uint32_t direct[INODE_DIRECT];
    uint32_t single_indirect;
    uint32_t double_indirect;
    uint32_t triple_indirect;
    uint8_t  type;            
    char main_tag[TAG_LEN];
    char tags[MAX_TAGS][TAG_LEN];
    uint8_t unused[15];
} inode_t;

struct dirent_c {
    uint32_t inode;
    char name[28];
};
#pragma pack(pop)

_Static_assert(sizeof(inode_t) == INODE_SIZE, "inode_t must be 128 bytes");
_Static_assert(sizeof(struct dirent_c) == 32, "dirent must be 32 bytes");

static int g_fd = -1;
static superblock_t g_sb;
static uint8_t  g_block_bitmap[BLOCK_BITMAP_MAX_SIZE];
static uint8_t  g_inode_bitmap[INODE_BITMAP_SIZE];
static uint32_t g_block_bitmap_bytes;
static uint32_t g_block_bitmap_sectors;
static uint32_t g_inode_bitmap_sectors;
static uint32_t g_inode_table_blocks;
static int      g_dirty_sb = 0;

static void die(const char *msg) {
    fprintf(stderr, "error: %s\n", msg);
    exit(1);
}

static void block_read(uint32_t lba, uint8_t *buf) {
    off_t off = (off_t)lba * SECTOR_SIZE;
    if (lseek(g_fd, off, SEEK_SET) < 0) die("seek failed (read)");
    ssize_t n = read(g_fd, buf, SECTOR_SIZE);
    if (n < 0) die("read failed");
    if (n < SECTOR_SIZE) memset(buf + n, 0, SECTOR_SIZE - n); /* past EOF */
}

static void block_write(uint32_t lba, const uint8_t *buf) {
    off_t off = (off_t)lba * SECTOR_SIZE;
    if (lseek(g_fd, off, SEEK_SET) < 0) die("seek failed (write)");
    ssize_t n = write(g_fd, buf, SECTOR_SIZE);
    if (n != SECTOR_SIZE) die("write failed");
}

static void zero_block(uint32_t lba) {
    uint8_t z[SECTOR_SIZE] = {0};
    block_write(lba, z);
}

static int get_block_bit(uint32_t idx) {
    if (idx >= g_sb.total_blocks) return 1;
    return (g_block_bitmap[idx / 8] >> (idx % 8)) & 1;
}
static void set_block_bit(uint32_t idx) {
    if (idx >= g_sb.total_blocks) return;
    g_block_bitmap[idx / 8] |= (1 << (idx % 8));
}
static void clear_block_bit(uint32_t idx) {
    if (idx >= g_sb.total_blocks) return;
    g_block_bitmap[idx / 8] &= ~(1 << (idx % 8));
}

static void load_block_bitmap(void) {
    uint32_t left = g_block_bitmap_bytes;
    uint8_t *dst = g_block_bitmap;
    uint8_t tmp[SECTOR_SIZE];
    for (uint32_t i = 0; i < g_block_bitmap_sectors; i++) {
        block_read(g_sb.bitmap_start + i, tmp);
        uint32_t n = left > SECTOR_SIZE ? SECTOR_SIZE : left;
        if (n) memcpy(dst, tmp, n);
        dst += n;
        left = (left > SECTOR_SIZE) ? left - SECTOR_SIZE : 0;
    }
}
static void save_block_bitmap(void) {
    uint32_t left = g_block_bitmap_bytes;
    uint8_t *src = g_block_bitmap;
    uint8_t tmp[SECTOR_SIZE];
    for (uint32_t i = 0; i < g_block_bitmap_sectors; i++) {
        uint32_t n = left > SECTOR_SIZE ? SECTOR_SIZE : left;
        memset(tmp, 0, SECTOR_SIZE);
        if (n) memcpy(tmp, src, n);
        block_write(g_sb.bitmap_start + i, tmp);
        src += n;
        left = (left > SECTOR_SIZE) ? left - SECTOR_SIZE : 0;
    }
}
static void load_inode_bitmap(void) {
    uint32_t start = g_sb.bitmap_start + g_block_bitmap_sectors;
    for (uint32_t i = 0; i < g_inode_bitmap_sectors; i++)
        block_read(start + i, g_inode_bitmap + i * SECTOR_SIZE);
}
static void save_inode_bitmap(void) {
    uint32_t start = g_sb.bitmap_start + g_block_bitmap_sectors;
    for (uint32_t i = 0; i < g_inode_bitmap_sectors; i++)
        block_write(start + i, g_inode_bitmap + i * SECTOR_SIZE);
}

static int alloc_block(void) {
    uint32_t start_idx = (g_sb.magic == FS_MAGIC && g_sb.data_start > 1) ? g_sb.data_start : 1;
    for (uint32_t i = start_idx; i < g_sb.total_blocks; i++) {
        if (!get_block_bit(i)) {
            set_block_bit(i);
            save_block_bitmap();
            return (int)i;
        }
    }
    return 0;
}
static void free_block(uint32_t idx) {
    clear_block_bit(idx);
    save_block_bitmap();
}
static int alloc_inode(void) {
    for (uint32_t i = 0; i < g_sb.inode_count; i++) {
        if (!(g_inode_bitmap[i / 8] & (1 << (i % 8)))) {
            g_inode_bitmap[i / 8] |= (1 << (i % 8));
            save_inode_bitmap();
            return (int)i;
        }
    }
    return -1;
}
static void free_inode(int idx) {
    if (idx < 0 || (uint32_t)idx >= g_sb.inode_count) return;
    g_inode_bitmap[idx / 8] &= ~(1 << (idx % 8));
    save_inode_bitmap();
}

static void read_inode(int idx, inode_t *inode) {
    uint8_t buf[SECTOR_SIZE];
    uint32_t block = g_sb.inode_start + idx / INODES_PER_BLOCK;
    uint32_t off = (idx % INODES_PER_BLOCK) * INODE_SIZE;
    block_read(block, buf);
    memcpy(inode, buf + off, sizeof(inode_t));
}
static void write_inode(int idx, inode_t *inode) {
    uint8_t buf[SECTOR_SIZE];
    uint32_t block = g_sb.inode_start + idx / INODES_PER_BLOCK;
    uint32_t off = (idx % INODES_PER_BLOCK) * INODE_SIZE;
    block_read(block, buf);
    memcpy(buf + off, inode, sizeof(inode_t));
    block_write(block, buf);
}

static uint32_t get_physical_block(uint32_t inode_idx, inode_t *node, uint32_t logical_idx, int do_alloc) {
    uint32_t ptrs[PTRS_PER_BLOCK];

    if (logical_idx < INODE_DIRECT) {
        if (node->direct[logical_idx] == 0 && do_alloc) {
            node->direct[logical_idx] = alloc_block();
            write_inode(inode_idx, node);
        }
        return node->direct[logical_idx];
    }
    logical_idx -= INODE_DIRECT;

    if (logical_idx < PTRS_PER_BLOCK) {
        if (node->single_indirect == 0 && do_alloc) {
            node->single_indirect = alloc_block();
            zero_block(node->single_indirect);
            write_inode(inode_idx, node);
        }
        if (node->single_indirect == 0) return 0;
        block_read(node->single_indirect, (uint8_t *)ptrs);
        if (ptrs[logical_idx] == 0 && do_alloc) {
            ptrs[logical_idx] = alloc_block();
            block_write(node->single_indirect, (uint8_t *)ptrs);
        }
        return ptrs[logical_idx];
    }
    logical_idx -= PTRS_PER_BLOCK;

    uint32_t double_limit = PTRS_PER_BLOCK * PTRS_PER_BLOCK;
    if (logical_idx < double_limit) {
        if (node->double_indirect == 0 && do_alloc) {
            node->double_indirect = alloc_block();
            zero_block(node->double_indirect);
            write_inode(inode_idx, node);
        }
        if (node->double_indirect == 0) return 0;
        uint32_t l1 = logical_idx / PTRS_PER_BLOCK;
        uint32_t l2 = logical_idx % PTRS_PER_BLOCK;
        block_read(node->double_indirect, (uint8_t *)ptrs);
        if (ptrs[l1] == 0 && do_alloc) {
            ptrs[l1] = alloc_block();
            zero_block(ptrs[l1]);
            block_write(node->double_indirect, (uint8_t *)ptrs);
        }
        if (ptrs[l1] == 0) return 0;
        uint32_t data_ptrs[PTRS_PER_BLOCK];
        block_read(ptrs[l1], (uint8_t *)data_ptrs);
        if (data_ptrs[l2] == 0 && do_alloc) {
            data_ptrs[l2] = alloc_block();
            block_write(ptrs[l1], (uint8_t *)data_ptrs);
        }
        return data_ptrs[l2];
    }
    logical_idx -= double_limit;

    uint32_t triple_limit = PTRS_PER_BLOCK * PTRS_PER_BLOCK * PTRS_PER_BLOCK;
    if (logical_idx < triple_limit) {
        if (node->triple_indirect == 0 && do_alloc) {
            node->triple_indirect = alloc_block();
            zero_block(node->triple_indirect);
            write_inode(inode_idx, node);
        }
        if (node->triple_indirect == 0) return 0;
        uint32_t l1 = logical_idx / (PTRS_PER_BLOCK * PTRS_PER_BLOCK);
        uint32_t rem = logical_idx % (PTRS_PER_BLOCK * PTRS_PER_BLOCK);
        uint32_t l2 = rem / PTRS_PER_BLOCK;
        uint32_t l3 = rem % PTRS_PER_BLOCK;
        uint32_t l1p[PTRS_PER_BLOCK];
        block_read(node->triple_indirect, (uint8_t *)l1p);
        if (l1p[l1] == 0 && do_alloc) {
            l1p[l1] = alloc_block();
            zero_block(l1p[l1]);
            block_write(node->triple_indirect, (uint8_t *)l1p);
        }
        if (l1p[l1] == 0) return 0;
        uint32_t l2p[PTRS_PER_BLOCK];
        block_read(l1p[l1], (uint8_t *)l2p);
        if (l2p[l2] == 0 && do_alloc) {
            l2p[l2] = alloc_block();
            zero_block(l2p[l2]);
            block_write(l1p[l1], (uint8_t *)l2p);
        }
        if (l2p[l2] == 0) return 0;
        uint32_t l3p[PTRS_PER_BLOCK];
        block_read(l2p[l2], (uint8_t *)l3p);
        if (l3p[l3] == 0 && do_alloc) {
            l3p[l3] = alloc_block();
            block_write(l2p[l2], (uint8_t *)l3p);
        }
        return l3p[l3];
    }
    return 0;
}

static void free_indirect_tree(uint32_t lba, int depth) {
    if (lba == 0) return;
    if (depth > 0) {
        uint32_t ptrs[PTRS_PER_BLOCK];
        block_read(lba, (uint8_t *)ptrs);
        for (uint32_t i = 0; i < PTRS_PER_BLOCK; i++)
            if (ptrs[i] != 0) free_indirect_tree(ptrs[i], depth - 1);
    }
    free_block(lba);
}

static int dir_lookup(inode_t *dir, const char *name) {
    if (dir->type != 2) return -1;
    uint8_t buf[SECTOR_SIZE];
    int per_block = SECTOR_SIZE / sizeof(struct dirent_c);
    for (int b = 0; b < INODE_DIRECT; b++) {
        uint32_t lba = dir->direct[b];
        if (lba == 0) break;
        block_read(lba, buf);
        struct dirent_c *entries = (struct dirent_c *)buf;
        for (int i = 0; i < per_block; i++) {
            if (entries[i].inode == 0) continue;
            if (entries[i].inode >= g_sb.inode_count) continue;
            if (strcmp(entries[i].name, name) == 0) return (int)entries[i].inode;
        }
    }
    return -1;
}

static int dir_add(uint32_t dir_inode_id, inode_t *dir, const char *name, uint32_t inode_idx) {
    uint8_t buf[SECTOR_SIZE];
    for (int b = 0; b < INODE_DIRECT; b++) {
        if (dir->direct[b] == 0) {
            uint32_t nb = alloc_block();
            if (nb == 0) return -1;
            dir->direct[b] = nb;
            memset(buf, 0, SECTOR_SIZE);
            write_inode(dir_inode_id, dir);
        } else {
            block_read(dir->direct[b], buf);
        }
        struct dirent_c *entries = (struct dirent_c *)buf;
        for (int i = 0; i < (int)(SECTOR_SIZE / sizeof(struct dirent_c)); i++) {
            if (entries[i].inode == 0) {
                entries[i].inode = inode_idx;
                strncpy(entries[i].name, name, sizeof(entries[i].name) - 1);
                entries[i].name[sizeof(entries[i].name) - 1] = '\0';
                block_write(dir->direct[b], buf);
                return 0;
            }
        }
    }
    return -1;
}

static int dir_remove(inode_t *dir, const char *name) {
    if (dir->type != 2) return -1;
    uint8_t buf[SECTOR_SIZE];
    int per_block = SECTOR_SIZE / sizeof(struct dirent_c);
    for (int b = 0; b < INODE_DIRECT; b++) {
        if (dir->direct[b] == 0) continue;
        block_read(dir->direct[b], buf);
        struct dirent_c *entries = (struct dirent_c *)buf;
        for (int i = 0; i < per_block; i++) {
            if (entries[i].inode != 0 && strcmp(entries[i].name, name) == 0) {
                entries[i].inode = 0;
                memset(entries[i].name, 0, sizeof(entries[i].name));
                block_write(dir->direct[b], buf);
                return 0;
            }
        }
    }
    return -1;
}

static int resolve_path(const char *path, inode_t *inode_out,
                         inode_t *parent_out, uint32_t *parent_idx_out,
                         char *last_name_out) {
    char buf[1024];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    uint32_t cur_idx = ROOT_INODE;
    inode_t cur;
    read_inode(cur_idx, &cur);

    uint32_t parent_idx = ROOT_INODE;
    inode_t parent = cur;

    char *save = NULL;
    char *tok = strtok_r(buf, "/", &save);
    if (!tok) { 
        if (inode_out) *inode_out = cur;
        if (parent_out) *parent_out = parent;
        if (parent_idx_out) *parent_idx_out = parent_idx;
        if (last_name_out) last_name_out[0] = 0;
        return (int)cur_idx;
    }

    while (tok) {
        char *next = strtok_r(NULL, "/", &save);
        int found = dir_lookup(&cur, tok);
        if (found < 0) return -1;
        parent_idx = cur_idx;
        parent = cur;
        cur_idx = (uint32_t)found;
        read_inode(cur_idx, &cur);
        if (!next) {
            if (last_name_out) strcpy(last_name_out, tok);
            break;
        }
        if (cur.type != 2) return -1;
        tok = next;
    }
    if (inode_out) *inode_out = cur;
    if (parent_out) *parent_out = parent;
    if (parent_idx_out) *parent_idx_out = parent_idx;
    return (int)cur_idx;
}

static uint32_t fs_read(uint32_t inode_idx, inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    uint32_t bytes_read = 0;
    uint8_t sbuf[SECTOR_SIZE];
    if (offset > node->size) return 0;
    if (offset + size > node->size) size = node->size - offset;
    while (bytes_read < size) {
        uint32_t goff = offset + bytes_read;
        uint32_t bidx = goff / SECTOR_SIZE;
        uint32_t boff = goff % SECTOR_SIZE;
        uint32_t phys = get_physical_block(inode_idx, node, bidx, 0);
        if (phys == 0) memset(sbuf, 0, SECTOR_SIZE);
        else block_read(phys, sbuf);
        uint32_t chunk = SECTOR_SIZE - boff;
        if (chunk > size - bytes_read) chunk = size - bytes_read;
        memcpy(buffer + bytes_read, sbuf + boff, chunk);
        bytes_read += chunk;
    }
    return bytes_read;
}

static int fs_write(uint32_t inode_idx, uint32_t offset, const uint8_t *data, size_t len) {
    inode_t node;
    read_inode(inode_idx, &node);
    size_t written = 0;
    uint8_t buf[SECTOR_SIZE];
    while (written < len) {
        uint32_t goff = offset + (uint32_t)written;
        uint32_t bidx = goff / SECTOR_SIZE;
        uint32_t boff = goff % SECTOR_SIZE;
        uint32_t phys = get_physical_block(inode_idx, &node, bidx, 1);
        if (phys == 0) return -1;
        if (boff > 0 || (len - written) < SECTOR_SIZE) block_read(phys, buf);
        size_t chunk = SECTOR_SIZE - boff;
        if (chunk > len - written) chunk = len - written;
        memcpy(buf + boff, data + written, chunk);
        block_write(phys, buf);
        written += chunk;
    }
    if (offset + written > node.size) {
        node.size = (uint32_t)(offset + written);
        write_inode(inode_idx, &node);
    }
    return (int)written;
}

static void mount_image(const char *path) {
    g_fd = open(path, O_RDWR);
    if (g_fd < 0) { perror("open"); exit(1); }

    uint8_t sb_buf[SECTOR_SIZE];
    block_read(SUPERBLOCK_LBA, sb_buf);
    memcpy(&g_sb, sb_buf, sizeof(superblock_t));

    if (g_sb.magic != FS_MAGIC) {
        die("not a COS filesystem image (bad magic) - did you mean 'mkfs'?");
    }

    g_block_bitmap_bytes   = (g_sb.total_blocks + 7) / 8;
    g_block_bitmap_sectors = (g_block_bitmap_bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
    g_inode_bitmap_sectors = ((g_sb.inode_count + 7) / 8 + SECTOR_SIZE - 1) / SECTOR_SIZE;
    g_inode_table_blocks   = (g_sb.inode_count * INODE_SIZE + SECTOR_SIZE - 1) / SECTOR_SIZE;

    load_block_bitmap();
    load_inode_bitmap();
}

static const char *type_str(uint8_t t) {
    switch (t) { case 0: return "free"; case 1: return "file"; case 2: return "dir"; default: return "?"; }
}

static void cmd_info(void) {
    printf("magic          : 0x%08X %s\n", g_sb.magic, g_sb.magic == FS_MAGIC ? "(valid)" : "(INVALID)");
    printf("block_size     : %u\n", g_sb.block_size);
    printf("total_blocks   : %llu\n", (unsigned long long)g_sb.total_blocks);
    printf("inode_count    : %u\n", g_sb.inode_count);
    printf("bitmap_start   : %u\n", g_sb.bitmap_start);
    printf("inode_start    : %u\n", g_sb.inode_start);
    printf("data_start     : %u\n", g_sb.data_start);
    printf("block_bitmap   : %u bytes / %u sectors\n", g_block_bitmap_bytes, g_block_bitmap_sectors);
    printf("inode_bitmap   : %u sectors\n", g_inode_bitmap_sectors);
    printf("inode_table    : %u sectors\n", g_inode_table_blocks);

    uint32_t used_blocks = 0;
    for (uint32_t i = 0; i < g_sb.total_blocks; i++) if (get_block_bit(i)) used_blocks++;
    uint32_t used_inodes = 0;
    for (uint32_t i = 0; i < g_sb.inode_count; i++)
        if (g_inode_bitmap[i / 8] & (1 << (i % 8))) used_inodes++;
    printf("blocks used    : %u / %llu\n", used_blocks, (unsigned long long)g_sb.total_blocks);
    printf("inodes used    : %u / %u\n", used_inodes, g_sb.inode_count);
}

static void list_dir(inode_t *dir, const char *prefix, int recurse) {
    uint8_t buf[SECTOR_SIZE];
    int per_block = SECTOR_SIZE / sizeof(struct dirent_c);
    for (int b = 0; b < INODE_DIRECT; b++) {
        uint32_t lba = dir->direct[b];
        if (lba == 0) break;
        block_read(lba, buf);
        struct dirent_c *entries = (struct dirent_c *)buf;
        for (int i = 0; i < per_block; i++) {
            if (entries[i].inode == 0) continue;
            if (entries[i].inode >= g_sb.inode_count) continue;
            inode_t child;
            read_inode((int)entries[i].inode, &child);
            printf("%s%-4u %-5s %8u  %-*s tag=%s\n",
                   prefix, entries[i].inode, type_str(child.type), child.size,
                   28, entries[i].name, child.main_tag);
            if (recurse && child.type == 2) {
                char newprefix[1200];
                snprintf(newprefix, sizeof(newprefix), "%s  ", prefix);
                list_dir(&child, newprefix, recurse);
            }
        }
    }
}

static void cmd_ls(const char *path) {
    inode_t node;
    int idx = resolve_path(path ? path : "", &node, NULL, NULL, NULL);
    if (idx < 0) die("path not found");
    if (node.type != 2) die("not a directory");
    printf("inode type    size  name                         tag\n");
    list_dir(&node, "", 0);
}

static void cmd_tree(void) {
    inode_t root;
    read_inode(ROOT_INODE, &root);
    printf("/ (inode 0)\n");
    list_dir(&root, "  ", 1);
}

static void cmd_cat(const char *path) {
    inode_t node;
    int idx = resolve_path(path, &node, NULL, NULL, NULL);
    if (idx < 0) die("path not found");
    if (node.type != 1) die("not a regular file");
    uint8_t *buf = malloc(node.size ? node.size : 1);
    uint32_t n = fs_read((uint32_t)idx, &node, 0, node.size, buf);
    fwrite(buf, 1, n, stdout);
    free(buf);
}

static void cmd_get(const char *path, const char *outfile) {
    inode_t node;
    int idx = resolve_path(path, &node, NULL, NULL, NULL);
    if (idx < 0) die("path not found");
    if (node.type != 1) die("not a regular file");
    FILE *f = fopen(outfile, "wb");
    if (!f) { perror("fopen"); exit(1); }
    uint8_t *buf = malloc(node.size ? node.size : 1);
    uint32_t n = fs_read((uint32_t)idx, &node, 0, node.size, buf);
    fwrite(buf, 1, n, f);
    fclose(f);
    free(buf);
    fprintf(stderr, "extracted %u bytes -> %s\n", n, outfile);
}

static void split_dir_base(const char *path, char *dirpart, char *base) {
    const char *slash = strrchr(path, '/');
    if (!slash) { dirpart[0] = 0; strcpy(base, path); }
    else {
        size_t n = slash - path;
        memcpy(dirpart, path, n);
        dirpart[n] = 0;
        strcpy(base, slash + 1);
    }
}

static void cmd_put(const char *localfile, const char *path, const char *tag) {
    char dirpart[1024], base[300];
    split_dir_base(path, dirpart, base);
    if (strlen(base) == 0 || strlen(base) > 27) die("invalid target filename (max 27 chars)");

    inode_t parent_dir;
    int parent_idx = resolve_path(dirpart, &parent_dir, NULL, NULL, NULL);
    if (parent_idx < 0) die("target directory not found");
    if (parent_dir.type != 2) die("target parent is not a directory");
    if (dir_lookup(&parent_dir, base) >= 0) die("a file/dir with that name already exists there");

    FILE *f = fopen(localfile, "rb");
    if (!f) { perror("fopen"); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(sz > 0 ? sz : 1);
    if (sz > 0) fread(data, 1, sz, f);
    fclose(f);

    const char *use_tag = (tag && strlen(tag) > 0) ? tag : "normal";

    int idx = alloc_inode();
    if (idx < 0) die("no free inodes");
    inode_t node;
    memset(&node, 0, sizeof(node));
    node.type = 1;
    node.size = 0;
    strncpy(node.main_tag, use_tag, TAG_LEN);
    node.main_tag[TAG_LEN - 1] = 0;
    strncpy(node.tags[0], "normal", TAG_LEN);
    int b = alloc_block();
    if (b == 0) { free_inode(idx); die("no free blocks"); }
    node.direct[0] = (uint32_t)b;
    write_inode(idx, &node);

    if (sz > 0) {
        if (fs_write((uint32_t)idx, 0, data, (size_t)sz) < 0) die("write failed (out of space?)");
    }
    free(data);

    read_inode(parent_idx, &parent_dir); 
    if (dir_add((uint32_t)parent_idx, &parent_dir, base, (uint32_t)idx) != 0) {
        die("failed to add directory entry (directory full?)");
    }
    fprintf(stderr, "created %s (inode %d, %ld bytes, tag=%s)\n", path, idx, sz, use_tag);
}

static void cmd_mkdir(const char *path) {
    char dirpart[1024], base[300];
    split_dir_base(path, dirpart, base);
    if (strlen(base) == 0 || strlen(base) > 27) die("invalid directory name (max 27 chars)");

    inode_t parent_dir;
    int parent_idx = resolve_path(dirpart, &parent_dir, NULL, NULL, NULL);
    if (parent_idx < 0) die("parent directory not found");
    if (parent_dir.type != 2) die("parent is not a directory");
    if (dir_lookup(&parent_dir, base) >= 0) die("a file/dir with that name already exists there");

    int idx = alloc_inode();
    if (idx < 0) die("no free inodes");
    inode_t node;
    memset(&node, 0, sizeof(node));
    node.type = 2;
    node.size = 0;
    int b = alloc_block();
    if (b == 0) { free_inode(idx); die("no free blocks"); }
    node.direct[0] = (uint32_t)b;
    write_inode(idx, &node);
    zero_block(b);

    if (dir_add((uint32_t)parent_idx, &parent_dir, base, (uint32_t)idx) != 0) {
        die("failed to add directory entry (directory full?)");
    }
    fprintf(stderr, "created directory %s (inode %d)\n", path, idx);
}

static void cmd_rm(const char *path) {
    char dirpart[1024], base[300];
    split_dir_base(path, dirpart, base);

    inode_t parent_dir;
    int parent_idx = resolve_path(dirpart, &parent_dir, NULL, NULL, NULL);
    if (parent_idx < 0) die("parent directory not found");

    int idx = dir_lookup(&parent_dir, base);
    if (idx < 0) die("file not found");
    inode_t node;
    read_inode(idx, &node);
    if (node.type == 2) die("refusing to remove a directory (only files supported)");

    for (int i = 0; i < INODE_DIRECT; i++) {
        if (node.direct[i] != 0) { free_block(node.direct[i]); node.direct[i] = 0; }
    }
    if (node.single_indirect) { free_indirect_tree(node.single_indirect, 1); node.single_indirect = 0; }
    if (node.double_indirect) { free_indirect_tree(node.double_indirect, 2); node.double_indirect = 0; }
    if (node.triple_indirect) { free_indirect_tree(node.triple_indirect, 3); node.triple_indirect = 0; }
    free_inode(idx);
    dir_remove(&parent_dir, base);
    fprintf(stderr, "removed %s (inode %d)\n", path, idx);
}

static void walk_tags(inode_t *dir, const char *dirpath, const char *tag) {
    uint8_t buf[SECTOR_SIZE];
    int per_block = SECTOR_SIZE / sizeof(struct dirent_c);
    for (int b = 0; b < INODE_DIRECT; b++) {
        uint32_t lba = dir->direct[b];
        if (lba == 0) break;
        block_read(lba, buf);
        struct dirent_c *entries = (struct dirent_c *)buf;
        for (int i = 0; i < per_block; i++) {
            if (entries[i].inode == 0 || entries[i].inode >= g_sb.inode_count) continue;
            inode_t child;
            read_inode((int)entries[i].inode, &child);
            int match = (strncmp(child.main_tag, tag, TAG_LEN) == 0);
            for (int t = 0; t < MAX_TAGS && !match; t++)
                if (strncmp(child.tags[t], tag, TAG_LEN) == 0) match = 1;
            char full[1200];
            snprintf(full, sizeof(full), "%s%s%s", dirpath, dirpath[0] ? "/" : "", entries[i].name);
            if (match) printf("%-5u %-5s %s\n", entries[i].inode, type_str(child.type), full);
            if (child.type == 2) walk_tags(&child, full, tag);
        }
    }
}

static void cmd_tags(const char *tag) {
    inode_t root;
    read_inode(ROOT_INODE, &root);
    printf("inode type  path\n");
    walk_tags(&root, "", tag);
}

static void cmd_mkfs(const char *imgpath, uint64_t total_sectors, uint32_t inode_count_hint) {
    g_fd = open(imgpath, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (g_fd < 0) { perror("open"); exit(1); }
    if (ftruncate(g_fd, (off_t)total_sectors * SECTOR_SIZE) != 0) { perror("ftruncate"); exit(1); }

    memset(&g_sb, 0, sizeof(g_sb));
    g_sb.magic = FS_MAGIC;
    g_sb.block_size = SECTOR_SIZE;
    g_sb.total_blocks = total_sectors;
    if (g_sb.total_blocks > (uint64_t)BLOCK_BITMAP_MAX_SIZE * 8)
        g_sb.total_blocks = (uint64_t)BLOCK_BITMAP_MAX_SIZE * 8;
    if (g_sb.total_blocks / 4 > (uint64_t)INODE_BITMAP_SIZE * 8)
        g_sb.total_blocks = ((uint64_t)INODE_BITMAP_SIZE * 8) * 4;

    g_sb.inode_count = inode_count_hint ? inode_count_hint : (uint32_t)(g_sb.total_blocks / 4);
    g_sb.inode_count = (g_sb.inode_count + 7) & ~7u;
    if (g_sb.inode_count == 0) g_sb.inode_count = 8;

    g_sb.bitmap_start = 1;
    g_block_bitmap_bytes   = (uint32_t)((g_sb.total_blocks + 7) / 8);
    g_block_bitmap_sectors = (g_block_bitmap_bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
    g_inode_bitmap_sectors = ((g_sb.inode_count + 7) / 8 + SECTOR_SIZE - 1) / SECTOR_SIZE;
    g_sb.inode_start = g_sb.bitmap_start + g_block_bitmap_sectors + g_inode_bitmap_sectors;
    g_inode_table_blocks = (uint32_t)((g_sb.inode_count * INODE_SIZE + SECTOR_SIZE - 1) / SECTOR_SIZE);
    g_sb.data_start = g_sb.inode_start + g_inode_table_blocks;

    uint8_t sb_buf[SECTOR_SIZE];
    memset(sb_buf, 0, SECTOR_SIZE);
    memcpy(sb_buf, &g_sb, sizeof(superblock_t));
    block_write(SUPERBLOCK_LBA, sb_buf);

    memset(g_block_bitmap, 0, sizeof(g_block_bitmap));
    memset(g_inode_bitmap, 0, sizeof(g_inode_bitmap));
    for (uint32_t i = 0; i < g_sb.data_start; i++) set_block_bit(i);
    save_block_bitmap();
    save_inode_bitmap();

    g_inode_bitmap[0] |= 1;
    save_inode_bitmap();
    inode_t root; memset(&root, 0, sizeof(root));
    root.type = 2;
    int rb = alloc_block();
    if (rb == 0) die("mkfs: failed to allocate root data block");
    root.direct[0] = (uint32_t)rb;
    write_inode(0, &root);
    zero_block((uint32_t)rb);

    read_inode(0, &root);
    const char *defdirs[] = {"user", "data", "mount", "lock", "cosfiles"};
    for (int i = 0; i < 5; i++) {
        int idx = alloc_inode();
        if (idx < 0) die("mkfs: out of inodes creating default dirs");
        inode_t node; memset(&node, 0, sizeof(node));
        node.type = 2;
        int b = alloc_block();
        if (b == 0) die("mkfs: out of blocks creating default dirs");
        node.direct[0] = (uint32_t)b;
        write_inode(idx, &node);
        zero_block((uint32_t)b);
        read_inode(0, &root);
        dir_add(0, &root, defdirs[i], (uint32_t)idx);
    }

    printf("mkfs: created COS filesystem on %s\n", imgpath);
    printf("  total_blocks=%llu inode_count=%u data_start=%u\n",
           (unsigned long long)g_sb.total_blocks, g_sb.inode_count, g_sb.data_start);
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "cosutil - Crusader OS (COS) filesystem image tool\n\n"
        "Usage:\n"
        "  %s <disk.img> info\n"
        "  %s <disk.img> ls   [path]\n"
        "  %s <disk.img> tree\n"
        "  %s <disk.img> cat  <path>\n"
        "  %s <disk.img> get  <path> <local_out_file>\n"
        "  %s <disk.img> put  <local_in_file> <path> [tag]\n"
        "  %s <disk.img> mkdir <path>\n"
        "  %s <disk.img> rm   <path>\n"
        "  %s <disk.img> tags <tag>\n"
        "  %s <new.img> mkfs [total_sectors] [inode_count]\n",
        argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0);
}

int main(int argc, char **argv) {
    if (argc < 3) { usage(argv[0]); return 1; }
    const char *imgpath = argv[1];
    const char *cmd = argv[2];

    if (strcmp(cmd, "mkfs") == 0) {
        uint64_t total = argc > 3 ? strtoull(argv[3], NULL, 0) : 65536;
        uint32_t inodes = argc > 4 ? (uint32_t)strtoul(argv[4], NULL, 0) : 0;
        cmd_mkfs(imgpath, total, inodes);
        close(g_fd);
        return 0;
    }

    mount_image(imgpath);

    if (strcmp(cmd, "info") == 0) {
        cmd_info();
    } else if (strcmp(cmd, "ls") == 0) {
        cmd_ls(argc > 3 ? argv[3] : "");
    } else if (strcmp(cmd, "tree") == 0) {
        cmd_tree();
    } else if (strcmp(cmd, "cat") == 0) {
        if (argc < 4) { usage(argv[0]); return 1; }
        cmd_cat(argv[3]);
    } else if (strcmp(cmd, "get") == 0) {
        if (argc < 5) { usage(argv[0]); return 1; }
        cmd_get(argv[3], argv[4]);
    } else if (strcmp(cmd, "put") == 0) {
        if (argc < 5) { usage(argv[0]); return 1; }
        cmd_put(argv[3], argv[4], argc > 5 ? argv[5] : NULL);
    } else if (strcmp(cmd, "mkdir") == 0) {
        if (argc < 4) { usage(argv[0]); return 1; }
        cmd_mkdir(argv[3]);
    } else if (strcmp(cmd, "rm") == 0) {
        if (argc < 4) { usage(argv[0]); return 1; }
        cmd_rm(argv[3]);
    } else if (strcmp(cmd, "tags") == 0) {
        if (argc < 4) { usage(argv[0]); return 1; }
        cmd_tags(argv[3]);
    } else {
        usage(argv[0]);
        close(g_fd);
        return 1;
    }

    close(g_fd);
    return 0;
}