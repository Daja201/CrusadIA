#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include <stddef.h>

#define FAT32_SECTOR_SIZE   512
#define FAT32_MAX_NAME      13
#define FAT32_ATTR_RO       0x01
#define FAT32_ATTR_HIDDEN   0x02
#define FAT32_ATTR_SYSTEM   0x04
#define FAT32_ATTR_VOLID    0x08
#define FAT32_ATTR_DIR      0x10
#define FAT32_ATTR_ARCHIVE  0x20
#define FAT32_ATTR_LFN      0x0F

#define FAT32_CLUSTER_FREE  0x00000000
#define FAT32_CLUSTER_EOC   0x0FFFFFF8
#define FAT32_CLUSTER_BAD   0x0FFFFFF7

typedef struct {
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint32_t num_fats;
    uint32_t fat_size_sectors;
    uint32_t total_sectors;
    uint32_t root_cluster;
    uint32_t fsinfo_sector;


    uint32_t partition_lba;
    uint32_t fat_begin_lba;
    uint32_t cluster_begin_lba;
    uint32_t data_clusters;
    uint32_t free_cluster_hint;
    uint8_t  mounted;
} fat32_fs_t;

typedef struct {
    char     name[FAT32_MAX_NAME];
    uint8_t  attr;
    uint32_t first_cluster;
    uint32_t size;
} fat32_dirent_t;

int  fat32_mount(uint32_t partition_lba);
void fat32_unmount(void);

int fat32_format(uint32_t partition_lba, uint32_t total_sectors, uint8_t sectors_per_cluster);

int fat32_list_dir(uint32_t dir_cluster, fat32_dirent_t* out, int out_max);

int fat32_stat(const char* path, fat32_dirent_t* out);

uint32_t fat32_read(const fat32_dirent_t* file, uint32_t offset, uint32_t size, uint8_t* buffer);

int fat32_write_file(uint32_t dir_cluster, const char* name, const uint8_t* data, uint32_t len);

int fat32_append_file(uint32_t dir_cluster, const char* name, const uint8_t* data, uint32_t len);

int fat32_create_dir(uint32_t dir_cluster, const char* name);

int fat32_delete_file(uint32_t dir_cluster, const char* name);

extern fat32_fs_t g_fat32;

#endif