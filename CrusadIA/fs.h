#ifndef FS_H
#define FS_H
#include <stdint.h>
#include <stddef.h>
#define INODE_DIRECT 12
#define MAX_TAGS 3
#define TAG_LEN 12
#define MAX_DRIVES 4
#define BLOCK_BITMAP_MAX_SIZE 32768
#define INODE_BITMAP_SIZE 32768

typedef struct {
    uint32_t magic;
    uint32_t block_size;
    uint64_t total_blocks;
    uint32_t inode_count;
    uint32_t bitmap_start;
    uint32_t inode_start;
    uint32_t data_start;
} __attribute__((packed)) superblock_t;

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
} __attribute__((packed)) inode_t;

typedef struct {
    uint8_t  drive_id;  
    uint16_t ata_base;  
    uint8_t  is_slave;    
    uint32_t total_sectors; 
    superblock_t sb;       
    uint8_t* block_bitmap;
    uint8_t  inode_bitmap[INODE_BITMAP_SIZE];
    uint32_t block_bitmap_bytes;
    uint32_t block_bitmap_sectors;
    uint32_t inode_bitmap_sectors;
    uint32_t inode_table_blocks;
} fs_device_t;
extern uint32_t g_current_dir;
extern fs_device_t g_drives[MAX_DRIVES];
extern int g_active_drives;
void init_fs(void);
void block_read(uint32_t lba, uint8_t* buf);
void block_write(uint32_t lba, const uint8_t* buf);
void load_block_bitmap(void);
void save_block_bitmap(void);
void load_inode_bitmap(void);
void save_inode_bitmap(void);
int alloc_block(void);
void free_block(uint32_t idx);
void create_root(void);
void read_inode(int idx, inode_t* inode);
void write_inode(int idx, inode_t* inode);
void free_inode(int idx);
uint32_t get_physical_block(uint32_t inode_idx, inode_t* node, uint32_t logical_idx, int do_alloc);
int dir_add(uint32_t dir_inode_id, inode_t* dir, const char* name, uint32_t inode_idx);
int dir_remove(inode_t* dir, const char* name);
int dir_lookup(inode_t* dir, const char* name);
uint32_t fs_create_file(const char* name, const char* main_tag);
int fs_create_dir(const char* name, uint32_t parent_inode_idx);
int fs_cd(const char* name);
int fs_write(uint32_t inode_idx, uint32_t offset, const uint8_t* data, size_t len);
uint32_t fs_read(uint32_t inode_idx, inode_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
int fs_delete_file(const char* path);
void format_fs(void);
void qformat_fs(void);
void create_defdirs(void);
int fs_find_by_tag(const char* tag, uint32_t* results, int max_results);
void drives(void);
#endif