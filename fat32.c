#include "fat32.h"
#include <string.h>
#include "klog.h"

extern void block_read(uint32_t lba, uint8_t* buf);
extern void block_write(uint32_t lba, const uint8_t* buf);

fat32_fs_t g_fat32;

#pragma pack(push, 1)
typedef struct {
    uint8_t  jmp[3];
    uint8_t  oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;

    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fsinfo_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved0[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];
} fat32_bpb_t;

typedef struct {
    uint32_t lead_sig;
    uint8_t  reserved1[480];
    uint32_t struct_sig;
    uint32_t free_count;
    uint32_t next_free;
    uint8_t  reserved2[12];
    uint32_t trail_sig;
} fat32_fsinfo_t;

typedef struct {
    uint8_t  name[11];
    uint8_t  attr;
    uint8_t  ntres;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_access_date;
    uint16_t first_cluster_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t first_cluster_lo;
    uint32_t file_size;
} fat32_direntry_t;
#pragma pack(pop)

#define DIRENTS_PER_SECTOR (FAT32_SECTOR_SIZE / sizeof(fat32_direntry_t))

static uint32_t cluster_to_lba(uint32_t cluster) {
    return g_fat32.cluster_begin_lba + (cluster - 2) * g_fat32.sectors_per_cluster;
}

static uint32_t fat_entry_get(uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t sector = g_fat32.fat_begin_lba + (fat_offset / FAT32_SECTOR_SIZE);
    uint32_t off = fat_offset % FAT32_SECTOR_SIZE;
    uint8_t buf[FAT32_SECTOR_SIZE];
    block_read(sector, buf);
    uint32_t val;
    memcpy(&val, buf + off, 4);
    return val & 0x0FFFFFFF;
}

static void fat_entry_set(uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t off = fat_offset % FAT32_SECTOR_SIZE;
    uint8_t buf[FAT32_SECTOR_SIZE];
    for (uint32_t f = 0; f < g_fat32.num_fats; f++) {
        uint32_t sector = g_fat32.fat_begin_lba + f * g_fat32.fat_size_sectors + (fat_offset / FAT32_SECTOR_SIZE);
        block_read(sector, buf);
        uint32_t old;
        memcpy(&old, buf + off, 4);
        uint32_t newv = (old & 0xF0000000) | (value & 0x0FFFFFFF);
        memcpy(buf + off, &newv, 4);
        block_write(sector, buf);
    }
}

static int fat_is_eoc(uint32_t v) {
    return v >= FAT32_CLUSTER_EOC || v == 0;
}

static uint32_t alloc_cluster(void) {
    uint32_t total = g_fat32.data_clusters + 2;
    uint32_t start = g_fat32.free_cluster_hint >= 2 ? g_fat32.free_cluster_hint : 2;
    for (uint32_t pass = 0; pass < 2; pass++) {
        uint32_t from = (pass == 0) ? start : 2;
        uint32_t to   = (pass == 0) ? total : start;
        for (uint32_t c = from; c < to; c++) {
            if (fat_entry_get(c) == FAT32_CLUSTER_FREE) {
                fat_entry_set(c, FAT32_CLUSTER_EOC);
                g_fat32.free_cluster_hint = c + 1;
                uint8_t zero[FAT32_SECTOR_SIZE];
                memset(zero, 0, FAT32_SECTOR_SIZE);
                for (uint8_t s = 0; s < g_fat32.sectors_per_cluster; s++) {
                    block_write(cluster_to_lba(c) + s, zero);
                }
                return c;
            }
        }
    }
    return 0;
}

static void free_chain(uint32_t cluster) {
    while (cluster >= 2 && !fat_is_eoc(cluster)) {
        uint32_t next = fat_entry_get(cluster);
        fat_entry_set(cluster, FAT32_CLUSTER_FREE);
        cluster = next;
    }
}

static void name_from_83(const uint8_t raw[11], char* out) {
    int p = 0;
    for (int i = 0; i < 8 && raw[i] != ' '; i++) out[p++] = (char)raw[i];
    int has_ext = 0;
    for (int i = 8; i < 11; i++) if (raw[i] != ' ') { has_ext = 1; break; }
    if (has_ext) {
        out[p++] = '.';
        for (int i = 8; i < 11 && raw[i] != ' '; i++) out[p++] = (char)raw[i];
    }
    out[p] = '\0';
}

static char up(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

static void name_to_83(const char* name, uint8_t out[11]) {
    memset(out, ' ', 11);
    int i = 0, oi = 0;
    while (name[i] && name[i] != '.' && oi < 8) { out[oi++] = (uint8_t)up(name[i]); i++; }
    while (name[i] && name[i] != '.') i++;
    if (name[i] == '.') {
        i++;
        int ei = 0;
        while (name[i] && ei < 3) { out[8 + ei] = (uint8_t)up(name[i]); ei++; i++; }
    }
}

typedef int (*dirent_cb)(fat32_direntry_t* de, uint32_t sector_lba, int slot, void* ctx);

static void for_each_dirent(uint32_t dir_cluster, dirent_cb cb, void* ctx) {
    if (dir_cluster == 0) dir_cluster = g_fat32.root_cluster;
    uint32_t cluster = dir_cluster;
    uint8_t buf[FAT32_SECTOR_SIZE];
    while (cluster >= 2 && !fat_is_eoc(cluster)) {
        uint32_t base_lba = cluster_to_lba(cluster);
        for (uint8_t s = 0; s < g_fat32.sectors_per_cluster; s++) {
            uint32_t lba = base_lba + s;
            block_read(lba, buf);
            fat32_direntry_t* ents = (fat32_direntry_t*)buf;
            for (int i = 0; i < (int)DIRENTS_PER_SECTOR; i++) {
                if (cb(&ents[i], lba, i, ctx)) return;
            }
        }
        cluster = fat_entry_get(cluster);
    }
}

#define FAT32_CLN_SHUT_BIT_MASK 0x08000000
#define FAT32_HRD_ERR_BIT_MASK  0x04000000

static void fat32_set_shutdown_bit(int clean) {
    uint32_t fat_offset = 1 * 4;
    uint32_t off = fat_offset % FAT32_SECTOR_SIZE;
    uint8_t buf[FAT32_SECTOR_SIZE];
    for (uint32_t f = 0; f < g_fat32.num_fats; f++) {
        uint32_t sector = g_fat32.fat_begin_lba + f * g_fat32.fat_size_sectors + (fat_offset / FAT32_SECTOR_SIZE);
        block_read(sector, buf);
        uint32_t val;
        memcpy(&val, buf + off, 4);
        if (clean) val |= FAT32_CLN_SHUT_BIT_MASK;
        else       val &= ~FAT32_CLN_SHUT_BIT_MASK;
        memcpy(buf + off, &val, 4);
        block_write(sector, buf);
    }
}

int fat32_mount(uint32_t partition_lba) {
    uint8_t sec[FAT32_SECTOR_SIZE];
    block_read(partition_lba, sec);
    fat32_bpb_t* bpb = (fat32_bpb_t*)sec;

    if (bpb->bytes_per_sector != FAT32_SECTOR_SIZE) return -1;
    if (bpb->fat_size_32 == 0) return -1;
    if (sec[510] != 0x55 || sec[511] != 0xAA) return -1;

    memset(&g_fat32, 0, sizeof(g_fat32));
    g_fat32.partition_lba      = partition_lba;
    g_fat32.bytes_per_sector   = bpb->bytes_per_sector;
    g_fat32.sectors_per_cluster= bpb->sectors_per_cluster;
    g_fat32.reserved_sectors   = bpb->reserved_sectors;
    g_fat32.num_fats           = bpb->num_fats;
    g_fat32.fat_size_sectors   = bpb->fat_size_32;
    g_fat32.total_sectors      = bpb->total_sectors_32 ? bpb->total_sectors_32 : bpb->total_sectors_16;
    g_fat32.root_cluster       = bpb->root_cluster;
    g_fat32.fsinfo_sector      = bpb->fsinfo_sector;

    g_fat32.fat_begin_lba     = partition_lba + g_fat32.reserved_sectors;
    g_fat32.cluster_begin_lba = g_fat32.fat_begin_lba + (g_fat32.num_fats * g_fat32.fat_size_sectors);
    uint32_t data_sectors = g_fat32.total_sectors - (g_fat32.cluster_begin_lba - partition_lba);
    g_fat32.data_clusters = data_sectors / g_fat32.sectors_per_cluster;

    g_fat32.free_cluster_hint = 2;
    if (g_fat32.fsinfo_sector) {
        uint8_t fi[FAT32_SECTOR_SIZE];
        block_read(partition_lba + g_fat32.fsinfo_sector, fi);
        fat32_fsinfo_t* info = (fat32_fsinfo_t*)fi;
        if (info->lead_sig == 0x41615252 && info->next_free != 0xFFFFFFFF && info->next_free >= 2) {
            g_fat32.free_cluster_hint = info->next_free;
        }
    }

    g_fat32.mounted = 1;
    fat32_set_shutdown_bit(0);
    kklog_color("FAT32 volume mounted", 0x00FF00);
    return 0;
}

void fat32_unmount(void) {
    if (g_fat32.mounted) {
        fat32_set_shutdown_bit(1);
    }
    g_fat32.mounted = 0;
}

static uint8_t pick_spc(uint32_t total_sectors) {
    uint64_t bytes = (uint64_t)total_sectors * FAT32_SECTOR_SIZE;
    if (bytes <= (uint64_t)8  * 1024 * 1024 * 1024) return 8;
    if (bytes <= (uint64_t)16 * 1024 * 1024 * 1024) return 16;
    if (bytes <= (uint64_t)32 * 1024 * 1024 * 1024) return 32;
    return 64;
}

int fat32_format(uint32_t partition_lba, uint32_t total_sectors, uint8_t sectors_per_cluster) {
    if (total_sectors < 66600) return -1;
    if (sectors_per_cluster == 0) sectors_per_cluster = pick_spc(total_sectors);

    const uint16_t reserved_sectors = 32;
    const uint8_t  num_fats = 2;


    uint32_t tmpval1 = total_sectors - reserved_sectors;
    uint32_t tmpval2 = ((256 * sectors_per_cluster) + num_fats) / 2;
    uint32_t fat_size = (tmpval1 + tmpval2 - 1) / tmpval2;

    uint32_t cluster_begin_rel = reserved_sectors + (num_fats * fat_size);
    if (cluster_begin_rel >= total_sectors) return -1;
    uint32_t data_clusters = (total_sectors - cluster_begin_rel) / sectors_per_cluster;
    if (data_clusters < 65525) return -1;


    uint8_t sec[FAT32_SECTOR_SIZE];
    memset(sec, 0, FAT32_SECTOR_SIZE);
    fat32_bpb_t* bpb = (fat32_bpb_t*)sec;
    bpb->jmp[0] = 0xEB; bpb->jmp[1] = 0x58; bpb->jmp[2] = 0x90;
    memcpy(bpb->oem, "CRUSADER", 8);
    bpb->bytes_per_sector = FAT32_SECTOR_SIZE;
    bpb->sectors_per_cluster = sectors_per_cluster;
    bpb->reserved_sectors = reserved_sectors;
    bpb->num_fats = num_fats;
    bpb->root_entry_count = 0;
    bpb->total_sectors_16 = 0;
    bpb->media = 0xF8;
    bpb->fat_size_16 = 0;
    bpb->sectors_per_track = 63;
    bpb->num_heads = 255;
    bpb->hidden_sectors = partition_lba;
    bpb->total_sectors_32 = total_sectors;
    bpb->fat_size_32 = fat_size;
    bpb->ext_flags = 0;
    bpb->fs_version = 0;
    bpb->root_cluster = 2;
    bpb->fsinfo_sector = 1;
    bpb->backup_boot_sector = 6;
    bpb->drive_number = 0x80;
    bpb->boot_signature = 0x29;
    bpb->volume_id = 0x12345678;
    memcpy(bpb->volume_label, "NO NAME    ", 11);
    memcpy(bpb->fs_type, "FAT32   ", 8);
    sec[510] = 0x55; sec[511] = 0xAA;
    block_write(partition_lba, sec);
    block_write(partition_lba + 6, sec);


    uint8_t fi[FAT32_SECTOR_SIZE];
    memset(fi, 0, FAT32_SECTOR_SIZE);
    fat32_fsinfo_t* info = (fat32_fsinfo_t*)fi;
    info->lead_sig = 0x41615252;
    info->struct_sig = 0x61417272;
    info->free_count = data_clusters - 1;
    info->next_free = 3;
    info->trail_sig = 0xAA550000;
    fi[510] = 0x55; fi[511] = 0xAA;
    block_write(partition_lba + 1, fi);
    block_write(partition_lba + 7, fi);


    uint8_t zero[FAT32_SECTOR_SIZE];
    memset(zero, 0, FAT32_SECTOR_SIZE);
    uint32_t fat_begin = partition_lba + reserved_sectors;
    for (uint8_t f = 0; f < num_fats; f++) {
        for (uint32_t i = 0; i < fat_size; i++) {
            block_write(fat_begin + f * fat_size + i, zero);
        }
    }
    uint8_t fat0[FAT32_SECTOR_SIZE];
    memset(fat0, 0, FAT32_SECTOR_SIZE);
    uint32_t e0 = 0x0FFFFFF8, e1 = 0x0FFFFFFF, e2 = 0x0FFFFFFF;
    memcpy(fat0 + 0, &e0, 4);
    memcpy(fat0 + 4, &e1, 4);
    memcpy(fat0 + 8, &e2, 4);
    for (uint8_t f = 0; f < num_fats; f++) {
        block_write(fat_begin + f * fat_size, fat0);
    }


    uint32_t cluster_begin = fat_begin + num_fats * fat_size;
    for (uint8_t s = 0; s < sectors_per_cluster; s++) {
        block_write(cluster_begin + s, zero);
    }

    kklog_color("FAT32 format complete", 0x00FF00);
    return fat32_mount(partition_lba);
}

struct list_ctx { fat32_dirent_t* out; int max; int count; };

static int list_cb(fat32_direntry_t* de, uint32_t lba, int slot, void* vctx) {
    (void)lba; (void)slot;
    struct list_ctx* ctx = (struct list_ctx*)vctx;
    if (de->name[0] == 0x00) return 1;
    if (de->name[0] == 0xE5) return 0;
    if (de->attr == FAT32_ATTR_LFN) return 0;
    if (de->attr & FAT32_ATTR_VOLID) return 0;
    if (ctx->count >= ctx->max) return 1;
    fat32_dirent_t* o = &ctx->out[ctx->count];
    name_from_83(de->name, o->name);
    o->attr = de->attr;
    o->first_cluster = ((uint32_t)de->first_cluster_hi << 16) | de->first_cluster_lo;
    o->size = de->file_size;
    ctx->count++;
    return 0;
}

int fat32_list_dir(uint32_t dir_cluster, fat32_dirent_t* out, int out_max) {
    if (!g_fat32.mounted) return -1;
    struct list_ctx ctx = { out, out_max, 0 };
    for_each_dirent(dir_cluster, list_cb, &ctx);
    return ctx.count;
}

struct find_ctx {
    const char* target;
    fat32_direntry_t found;
    uint32_t found_lba;
    int found_slot;
    int ok;
};

static int find_cb(fat32_direntry_t* de, uint32_t lba, int slot, void* vctx) {
    struct find_ctx* ctx = (struct find_ctx*)vctx;
    if (de->name[0] == 0x00) return 1;
    if (de->name[0] == 0xE5) return 0;
    if (de->attr == FAT32_ATTR_LFN) return 0;
    char name[FAT32_MAX_NAME];
    name_from_83(de->name, name);

    int i = 0;
    for (; name[i] && ctx->target[i]; i++) {
        if (up(name[i]) != up(ctx->target[i])) break;
    }
    if (name[i] == '\0' && ctx->target[i] == '\0') {
        ctx->found = *de;
        ctx->found_lba = lba;
        ctx->found_slot = slot;
        ctx->ok = 1;
        return 1;
    }
    return 0;
}

static int find_in_dir(uint32_t dir_cluster, const char* name, struct find_ctx* out) {
    memset(out, 0, sizeof(*out));
    out->target = name;
    for_each_dirent(dir_cluster, find_cb, out);
    return out->ok ? 0 : -1;
}

int fat32_stat(const char* path, fat32_dirent_t* out) {
    if (!g_fat32.mounted) return -1;
    uint32_t dir = g_fat32.root_cluster;
    char comp[FAT32_MAX_NAME];
    const char* p = path;
    if (*p == '/' || *p == '>') p++;
    struct find_ctx fc;
    fat32_direntry_t last = {0};
    int found_any = 0;
    while (*p) {
        int i = 0;
        while (p[i] && p[i] != '/' && p[i] != '>' && i < FAT32_MAX_NAME - 1) { comp[i] = p[i]; i++; }
        comp[i] = '\0';
        p += i;
        if (*p == '/' || *p == '>') p++;
        if (comp[0] == '\0') continue;
        if (find_in_dir(dir, comp, &fc) != 0) return -1;
        last = fc.found;
        found_any = 1;
        uint32_t clus = ((uint32_t)fc.found.first_cluster_hi << 16) | fc.found.first_cluster_lo;
        if (*p) {
            if (!(fc.found.attr & FAT32_ATTR_DIR)) return -1;
            dir = clus;
        }
    }
    if (!found_any) return -1;
    name_from_83(last.name, out->name);
    out->attr = last.attr;
    out->first_cluster = ((uint32_t)last.first_cluster_hi << 16) | last.first_cluster_lo;
    out->size = last.file_size;
    return 0;
}

uint32_t fat32_read(const fat32_dirent_t* file, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!g_fat32.mounted || !file) return 0;
    if (offset >= file->size) return 0;
    if (offset + size > file->size) size = file->size - offset;

    uint32_t cluster_bytes = g_fat32.sectors_per_cluster * FAT32_SECTOR_SIZE;
    uint32_t cluster_idx = offset / cluster_bytes;
    uint32_t cluster = file->first_cluster;
    for (uint32_t i = 0; i < cluster_idx && cluster >= 2 && !fat_is_eoc(cluster); i++) {
        cluster = fat_entry_get(cluster);
    }

    uint32_t bytes_read = 0;
    uint32_t offset_in_cluster = offset % cluster_bytes;
    uint8_t sec[FAT32_SECTOR_SIZE];
    while (bytes_read < size && cluster >= 2 && !fat_is_eoc(cluster)) {
        uint32_t base_lba = cluster_to_lba(cluster);
        uint32_t sec_in_cluster = offset_in_cluster / FAT32_SECTOR_SIZE;
        uint32_t off_in_sec = offset_in_cluster % FAT32_SECTOR_SIZE;
        while (bytes_read < size && sec_in_cluster < g_fat32.sectors_per_cluster) {
            block_read(base_lba + sec_in_cluster, sec);
            uint32_t chunk = FAT32_SECTOR_SIZE - off_in_sec;
            if (chunk > size - bytes_read) chunk = size - bytes_read;
            memcpy(buffer + bytes_read, sec + off_in_sec, chunk);
            bytes_read += chunk;
            off_in_sec = 0;
            sec_in_cluster++;
        }
        offset_in_cluster = 0;
        cluster = fat_entry_get(cluster);
    }
    return bytes_read;
}

struct free_slot_ctx { uint32_t lba; int slot; int found; };
static int free_slot_cb(fat32_direntry_t* de, uint32_t lba, int slot, void* vctx) {
    struct free_slot_ctx* ctx = (struct free_slot_ctx*)vctx;
    if (de->name[0] == 0x00 || de->name[0] == 0xE5) {
        ctx->lba = lba; ctx->slot = slot; ctx->found = 1;
        return 1;
    }
    return 0;
}

static int alloc_dirent_slot(uint32_t dir_cluster, uint32_t* out_lba, int* out_slot) {
    if (dir_cluster == 0) dir_cluster = g_fat32.root_cluster;
    struct free_slot_ctx ctx = {0};
    for_each_dirent(dir_cluster, free_slot_cb, &ctx);
    if (ctx.found) { *out_lba = ctx.lba; *out_slot = ctx.slot; return 0; }


    uint32_t cluster = dir_cluster;
    while (!fat_is_eoc(fat_entry_get(cluster))) cluster = fat_entry_get(cluster);
    uint32_t nc = alloc_cluster();
    if (nc == 0) return -1;
    fat_entry_set(cluster, nc);
    *out_lba = cluster_to_lba(nc);
    *out_slot = 0;
    return 0;
}

static void write_dirent(uint32_t lba, int slot, const char* name, uint8_t attr,
                          uint32_t first_cluster, uint32_t size) {
    uint8_t buf[FAT32_SECTOR_SIZE];
    block_read(lba, buf);
    fat32_direntry_t* ents = (fat32_direntry_t*)buf;
    fat32_direntry_t* de = &ents[slot];
    memset(de, 0, sizeof(*de));
    name_to_83(name, de->name);
    de->attr = attr;
    de->first_cluster_hi = (uint16_t)(first_cluster >> 16);
    de->first_cluster_lo = (uint16_t)(first_cluster & 0xFFFF);
    de->file_size = size;
    block_write(lba, buf);
}

int fat32_write_file(uint32_t dir_cluster, const char* name, const uint8_t* data, uint32_t len) {
    if (!g_fat32.mounted) return -1;
    struct find_ctx fc;
    uint32_t first_cluster;
    uint32_t entry_lba; int entry_slot;

    if (find_in_dir(dir_cluster, name, &fc) == 0) {

        uint32_t old = ((uint32_t)fc.found.first_cluster_hi << 16) | fc.found.first_cluster_lo;
        if (old >= 2) free_chain(old);
        entry_lba = fc.found_lba;
        entry_slot = fc.found_slot;
    } else {
        if (alloc_dirent_slot(dir_cluster, &entry_lba, &entry_slot) != 0) return -1;
    }

    first_cluster = 0;
    if (len > 0) {
        first_cluster = alloc_cluster();
        if (first_cluster == 0) return -1;
        uint32_t cluster_bytes = g_fat32.sectors_per_cluster * FAT32_SECTOR_SIZE;
        uint32_t written = 0;
        uint32_t cluster = first_cluster;
        uint8_t sec[FAT32_SECTOR_SIZE];
        while (written < len) {
            uint32_t in_cluster = 0;
            uint32_t base_lba = cluster_to_lba(cluster);
            while (in_cluster < cluster_bytes && written < len) {
                uint32_t chunk = FAT32_SECTOR_SIZE;
                if (chunk > len - written) chunk = len - written;
                memset(sec, 0, FAT32_SECTOR_SIZE);
                memcpy(sec, data + written, chunk);
                block_write(base_lba + (in_cluster / FAT32_SECTOR_SIZE), sec);
                written += chunk;
                in_cluster += FAT32_SECTOR_SIZE;
            }
            if (written < len) {
                uint32_t nc = alloc_cluster();
                if (nc == 0) break;
                fat_entry_set(cluster, nc);
                cluster = nc;
            }
        }
    }

    write_dirent(entry_lba, entry_slot, name, FAT32_ATTR_ARCHIVE, first_cluster, len);
    return 0;
}

int fat32_append_file(uint32_t dir_cluster, const char* name, const uint8_t* data, uint32_t len) {
    if (!g_fat32.mounted || len == 0) return len == 0 ? 0 : -1;
    struct find_ctx fc;
    if (find_in_dir(dir_cluster, name, &fc) != 0) {
        return fat32_write_file(dir_cluster, name, data, len);
    }
    uint32_t old_size = fc.found.file_size;
    uint32_t cluster = ((uint32_t)fc.found.first_cluster_hi << 16) | fc.found.first_cluster_lo;
    uint32_t cluster_bytes = g_fat32.sectors_per_cluster * FAT32_SECTOR_SIZE;

    if (cluster < 2) {
        cluster = alloc_cluster();
        if (cluster == 0) return -1;
        write_dirent(fc.found_lba, fc.found_slot, name, fc.found.attr, cluster, old_size);
    } else {
        while (!fat_is_eoc(fat_entry_get(cluster))) cluster = fat_entry_get(cluster);
    }

    uint32_t off_in_cluster = old_size % cluster_bytes;
    if (old_size > 0 && off_in_cluster == 0) {

        off_in_cluster = cluster_bytes;
    }

    uint32_t written = 0;
    uint8_t sec[FAT32_SECTOR_SIZE];
    while (written < len) {
        if (off_in_cluster >= cluster_bytes) {
            uint32_t nc = alloc_cluster();
            if (nc == 0) break;
            fat_entry_set(cluster, nc);
            cluster = nc;
            off_in_cluster = 0;
        }
        uint32_t base_lba = cluster_to_lba(cluster);
        uint32_t sec_idx = off_in_cluster / FAT32_SECTOR_SIZE;
        uint32_t off_in_sec = off_in_cluster % FAT32_SECTOR_SIZE;
        block_read(base_lba + sec_idx, sec);
        uint32_t chunk = FAT32_SECTOR_SIZE - off_in_sec;
        if (chunk > len - written) chunk = len - written;
        memcpy(sec + off_in_sec, data + written, chunk);
        block_write(base_lba + sec_idx, sec);
        written += chunk;
        off_in_cluster += chunk;
    }

    write_dirent(fc.found_lba, fc.found_slot, name, fc.found.attr, cluster >= 2 ? cluster : 0, old_size + written);
    return (written == len) ? 0 : -1;
}

int fat32_create_dir(uint32_t dir_cluster, const char* name) {
    if (!g_fat32.mounted) return -1;
    struct find_ctx fc;
    if (find_in_dir(dir_cluster, name, &fc) == 0) return -1;

    uint32_t new_cluster = alloc_cluster();
    if (new_cluster == 0) return -1;


    uint8_t buf[FAT32_SECTOR_SIZE];
    memset(buf, 0, FAT32_SECTOR_SIZE);
    fat32_direntry_t* ents = (fat32_direntry_t*)buf;
    memset(ents[0].name, ' ', 11); ents[0].name[0] = '.';
    ents[0].attr = FAT32_ATTR_DIR;
    uint32_t self = new_cluster;
    ents[0].first_cluster_hi = (uint16_t)(self >> 16);
    ents[0].first_cluster_lo = (uint16_t)(self & 0xFFFF);

    memset(ents[1].name, ' ', 11); ents[1].name[0] = '.'; ents[1].name[1] = '.';
    ents[1].attr = FAT32_ATTR_DIR;
    uint32_t parent = dir_cluster ? dir_cluster : g_fat32.root_cluster;
    if (dir_cluster == g_fat32.root_cluster || dir_cluster == 0) parent = 0;
    ents[1].first_cluster_hi = (uint16_t)(parent >> 16);
    ents[1].first_cluster_lo = (uint16_t)(parent & 0xFFFF);

    block_write(cluster_to_lba(new_cluster), buf);

    uint32_t entry_lba; int entry_slot;
    if (alloc_dirent_slot(dir_cluster, &entry_lba, &entry_slot) != 0) {
        free_chain(new_cluster);
        return -1;
    }
    write_dirent(entry_lba, entry_slot, name, FAT32_ATTR_DIR, new_cluster, 0);
    return 0;
}

int fat32_delete_file(uint32_t dir_cluster, const char* name) {
    if (!g_fat32.mounted) return -1;
    struct find_ctx fc;
    if (find_in_dir(dir_cluster, name, &fc) != 0) return -1;
    if (fc.found.attr & FAT32_ATTR_DIR) return -1;

    uint32_t cluster = ((uint32_t)fc.found.first_cluster_hi << 16) | fc.found.first_cluster_lo;
    if (cluster >= 2) free_chain(cluster);

    uint8_t buf[FAT32_SECTOR_SIZE];
    block_read(fc.found_lba, buf);
    fat32_direntry_t* ents = (fat32_direntry_t*)buf;
    ents[fc.found_slot].name[0] = 0xE5;
    block_write(fc.found_lba, buf);
    return 0;
}