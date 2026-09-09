#include "commands.h"
#include "terminal.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <cpuid.h>
#include "reboot.h"
#include "diskinfo.h"
#include "fs.h"
#include "library.h"
#include "klog.h"
#include "string.h"
#include "fs.h"
#include "rtc.h"
#include "vesa.h"
#include "pmm.h"
#include "pci.h"
#include "app.h"
#include "ac97.h"
#include "speaker.h"
#include "task.h"
#include "fat32.h"
#include "usb.h"
#include "usbhid.h"
#include "templar.h"

#define CHUNK_SIZE 65532
extern fs_device_t g_drives[MAX_DRIVES];
extern int g_current_drive;
extern int g_active_drives;
extern superblock_t g_superblock;
extern void select_drive(uint16_t base, uint8_t slave);
extern void block_read(uint32_t lba, uint8_t* buf);
extern int fs_change_drive(int drive_id);
extern void init_fs(void);
extern char g_current_path[];
extern uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
extern void pci_config_write(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);

extern uint32_t g_current_dir;
extern void read_inode(int idx, inode_t* inode);
extern int fs_resolve_path(const char* path, uint32_t current_dir_inode);
static uint8_t wav_audio_buffer[65536] __attribute__((aligned(8)));
extern int fs_resolve_path(const char* path, uint32_t current_dir_inode);
static uint8_t streaming_buffer[CHUNK_SIZE] __attribute__((aligned(8)));

struct ac97_bdl_entry {
    uint32_t buffer_addr;
    uint16_t length;
    uint16_t flags;
} __attribute__((packed));

static struct ac97_bdl_entry bdl[1] __attribute__((aligned(8)));
static int16_t audio_buffer[32000];
extern void select_drive(uint16_t base, uint8_t slave);
extern void block_read(uint32_t lba, uint8_t* buf);

#define CHUNK_SECTORS 128
static uint8_t raw_wav_buffer[CHUNK_SECTORS * 512] __attribute__((aligned(8)));

void cmd_help(int argc, char** argv) {
    kklog_color("Welcome to Crusader OS made by David Zapletal", 0xFF0000);
    kklog_color("Source code shoould be available on: https://github.com/Daja201/Crusader-OS-v03", 0xFF0000);
    kklog_color("Feel free to copy and change source code for yourself.", 0xFF0000);
    kklog_color("Run command: 'lib' for info about commands and whole system.", 0xFF0000);
}

void busy_ms(int ms) {
    volatile unsigned int iter = 8000 * ms;
    for (volatile unsigned int i = 0; i < iter; i++) asm volatile ("nop");
}

void cmd_cow(int argc, char** argv) {
    vesa_clear(0x000000);
    vesa_draw_rec(0, 1070, 1920, 10, 0x00FF00);
    vesa_swap();
    const char *cow[] = {
        "          (__) ",
        "          (oo) ",
        "   /-------\\/  ",
        "  / |     ||   ",
        " +  ||----||   ",
        "    ^^    ^^   "
    };
    const int cow_lines = 6;
    const int char_w = 8;
    const int char_h = 8;
    int cow_width_px = 15 * char_w;
    int x_start = (1080 - cow_width_px) / 2;
    if (x_start < 0) x_start = 0;
    int top_y = 10;
    int bottom_y = 1020;
    const int frames_per_jump = 30;
    const int ms_per_frame = 60;
    int prev_y = top_y;

    for (int iteration = 0; iteration < 10; iteration++) {
        for (int step = 0; step <= frames_per_jump; step++) {
            int current_y = top_y + ((bottom_y - top_y) * step / frames_per_jump);
            vesa_draw_rec(x_start, prev_y, cow_width_px, cow_lines * char_h, 0x000000);
            for (int i = 0; i < cow_lines; i++) {
                int line_y = current_y + (i * char_h);
                for (int j = 0; cow[i][j] != '\0'; j++) {
                    vesa_draw_char(cow[i][j], x_start + (j * char_w), line_y, 0xFFFFFF, 0x000000);
                }
            }
            vesa_swap();
            busy_ms(ms_per_frame);
            prev_y = current_y;
        }
        for (int step = frames_per_jump; step >= 0; step--) {
            int current_y = top_y + ((bottom_y - top_y) * step / frames_per_jump);
            vesa_draw_rec(x_start, prev_y, cow_width_px, cow_lines * char_h, 0x000000);
            for (int i = 0; i < cow_lines; i++) {
                int line_y = current_y + (i * char_h);
                for (int j = 0; cow[i][j] != '\0'; j++) {
                    vesa_draw_char(cow[i][j], x_start + (j * char_w), line_y, 0xFFFFFF, 0x000000);
                }
            }
            vesa_swap();
            busy_ms(ms_per_frame);
            prev_y = current_y;
        }
    }
    vesa_clear(0x000000);
    vesa_swap();
}

void cmd_mem() {
    uint32_t free_kb = pmm_count_mem();
    klogf("Free memory: %d KB (%d MB)\n", free_kb, free_kb / 1024);
}

void cmd_cat(int argc, char** argv) {
    const char *cat =
"    /\\___/\\   \n"
"   /       \\  \n"
"  |  u   u  | \n"
"--|----*----|--\n"
"   \\   w   /       \n"
"     ======\n"
"   /       \\ __   \n"
"   |        |\\ \\   \n"
"   |        |/ /     \n"
"   |  | |   | /   \n"
"   \\ ml lm /_/      \n";
    vesa_print_string(cat);
}

void cmd_ld(int argc, char** argv) {
    if (g_active_drives == 0) {
        kklog_color("No active drives detected.", 0xFF0000);
        return;
    }
    kklog_color("ALL DRIVES INFO:", 0x00FF00);
    int original_drive = g_current_drive;
    for (int i = 0; i < g_active_drives; i++) {
        fs_device_t* dev = &g_drives[i];
        klogf("Drive Index:  %d\n", i);
        klogf("ATA Base:     0x%x\n", (uint32_t)dev->ata_base);
        klogf("Type:         %s\n", dev->is_slave ? "Slave" : "Master");
        uint64_t sectors = dev->total_sectors;
        uint64_t mb = (sectors * 512) / (1024 * 1024);
        klogf("Capacity:     %d MB (%d sectors)\n", (uint32_t)mb, (uint32_t)sectors);
        select_drive(dev->ata_base, dev->is_slave);
        uint8_t sector_buf[512];
        block_read(0, sector_buf);
        superblock_t* temp_sb = (superblock_t*)sector_buf;
        if (temp_sb->magic == 0x5A4C534A) {
            if (i == original_drive) {
                kklog_color("Status:       Formatted [CURRENT]", 0x00FF00);
            } else {
                kklog_color("Status:       Formatted", 0x00FF00);
            }
            klogf("Total Blocks: %d\n", temp_sb->total_blocks);
            klogf("Inodes:       %d used/total\n", temp_sb->inode_count);
            klogf("Data Start:   LBA %d\n", temp_sb->data_start);
        } else if (sector_buf[510] == 0x55 && sector_buf[511] == 0xAA &&
                   (*(uint32_t*)(sector_buf + 36)) != 0) {
            uint16_t bytes_per_sector;
            uint8_t  sectors_per_cluster;
            uint32_t fat_size_32, root_cluster, total_sectors_32;
            memcpy(&bytes_per_sector, sector_buf + 11, 2);
            sectors_per_cluster = sector_buf[13];
            memcpy(&fat_size_32, sector_buf + 36, 4);
            memcpy(&root_cluster, sector_buf + 44, 4);
            memcpy(&total_sectors_32, sector_buf + 32, 4);
            if (i == original_drive) {
                kklog_color("Status:       Formatted FAT32 [CURRENT]", 0x00FF00);
            } else {
                kklog_color("Status:       Formatted FAT32", 0x00FF00);
            }
            klogf("Bytes/Sector: %d\n", bytes_per_sector);
            klogf("Sect/Cluster: %d\n", sectors_per_cluster);
            klogf("FAT Size:     %d sectors\n", fat_size_32);
            klogf("Root Cluster: %d\n", root_cluster);
            klogf("Total Sect:   %d\n", total_sectors_32);
        } else {
            if (i == original_drive) {
                kklog_color("Status:       NOT FORMATTED [CURRENT]", 0xFF0000);
                kklog("use 'format' to format current drive");
            } else {
                kklog_color("Status:       NOT FORMATTED", 0xFF0000);
            }

        }
        klog("\n");
        vesa_draw_hor(c_x, c_y + 4, 500, 0xFFFFFF);
        c_y = c_y + 8;
    }
    select_drive(g_drives[original_drive].ata_base, g_drives[original_drive].is_slave);
}

void cmd_clear(int argc, char** argv) {
    vesa_clear(0x000000);
}

void cmd_reboot(int argc, char** argv) {
    kklog("reboot in process\n");
    reboot_triple_fault();

}

void cmd_read_custom(int argc, char** argv) {
    if (argc < 2) {
        kklog("Usage: read <filename>");
        return;
    }
    inode_t root;
    read_inode(g_current_dir, &root);
    int inode_num = dir_lookup(&root, argv[1]);
    if (inode_num < 0) {
        klog_status("ERROR FILE NOT FOUND", 0xff0000);
        return;
    }
    inode_t file_node;
    read_inode(inode_num, &file_node);
    uint8_t buf[32768];
    uint32_t to_read = file_node.size;
    if (to_read > sizeof(buf)) {
        to_read = sizeof(buf);
    }
    int bytes_read = fs_read(inode_num, &file_node, 0, to_read, buf);
    if (bytes_read <= 0) {
        klog_status("ERROR COULD NOT READ FILE OR EMPTY", 0xFF0000);
        return;
    }
    for (int i = 0; i < bytes_read; i++) {
        if (buf[i] == '\0') break;
        char ch[2] = {buf[i], '\0'};
        vesa_print_string(ch);
    }
    vesa_print_string("\n");
}

void cmd_ls_custom(int argc, char** argv) {
    inode_t dir;
    read_inode(g_current_dir, &dir);
    uint8_t buf[512];
    struct dirent {
        uint32_t inode;
        char name[28];
    };
    int entry_count = 512 / sizeof(struct dirent);
    for (int b = 0; b < 12; b++) {
        uint32_t block_lba = dir.direct[b];
        if (block_lba == 0) continue;
        block_read(block_lba, buf);
        struct dirent* entries = (struct dirent*)buf;
        for (int i = 0; i < entry_count; i++) {
            if (entries[i].inode != 0 && entries[i].inode < g_superblock.inode_count) {
                inode_t file_node;
                read_inode(entries[i].inode, &file_node);
                klog("  ");
                if (file_node.type == 2) {
                    klog_color(entries[i].name, 0xFF0000);
                } else {
                    klog_color(entries[i].name, 0x00FF00);
                }
            }
        }
        klog("\n");
    }
}

void cmd_dl_custom(int argc, char** argv) {
    if (argc < 2) {
        kklog("usage: dl <file>");
        return;
    }
    if (fs_delete_file(argv[1]) < 0) {
        kklog("dl: failed");
        return;
    }
    kklog("file deleted");
}

void cmd_wr_custom(int argc, char** argv) {
    if (argc < 3) {
        kklog("Usage: wr <filename> <data>");
        return;
    }
    const char* wr = "wr";
    const char* filename = argv[1];
    const char* data = argv[2];
    uint32_t inode = fs_create_file(filename, wr);
    if ((int32_t)inode < 0) {
        kklog("file creation failed");
        return;
    }
    inode_t node;
    read_inode(inode, &node);
    int written = fs_write(inode, node.size,(const uint8_t*)data, strlen(data));
    if (written < 0) {
        kklog("write failed");
    } else {
        kklog("write successful");
    }
}

void cmd_shutdown(int argc, char** argv) {
    kklog_color("\nPREPARING FS...\n", 0xFF0000);
    save_block_bitmap();
    save_inode_bitmap();
    busy_ms(1000);
    asm volatile ("cli");
    vesa_draw_rec(0, 0, 1920, 1080, 0x000000);
    const char* verse = "When you lie down, you will not be afraid. Yes, you will lie down, and your sleep will be sweet. (PROVERBS 3:24)";
    const char* msg = "IT IS NOW SAFE TO TURN OFF YOUR COMPUTER. GOOD NIGHT :)";
    int start_x_msg = (1920 - (strlen(msg) * 8)) / 2;
    int start_y_msg = 350;
    int start_x_verse = (1920 - (strlen(verse) * 8)) / 2;
    int start_y_verse = start_y_msg - 20;
    for(int i = 0; verse[i] != '\0'; i++) {
        vesa_draw_char(verse[i], start_x_verse + (i * 8), start_y_verse, 0x2BC7FB, 0x000000);
    }
    for(int i = 0; msg[i] != '\0'; i++) {
        vesa_draw_char(msg[i], start_x_msg + (i * 8), start_y_msg, 0x2BC7FB, 0x000000);
    }
    vesa_swap();
    for (;;) {
        asm volatile ("hlt");
    }
}

void cmd_time(int argc, char** argv) {
    int year, month, day;
    int hour, min, sec;
    rtc_get_datetime(&year, &month, &day, &hour, &min, &sec);
    char b[8];
    klog_color("RTC: ", 0x009000);
    itoa(year, b, 10); klog_color(b, 0x009000);;itoa(month, b, 10);klog_color(" ", 0x009000); klog_color(b, 0x009000);itoa(day, b, 10);klog_color(" ", 0x009000); klog_color(b, 0x009000); klog_color(" ", 0x009000);
    itoa(hour, b, 10); klog_color(b, 0x009000); klog_color(":", 0x009000);
    itoa(min, b, 10); klog_color(b, 0x009000); klog_color(":", 0x009000);
    itoa(sec, b, 10); klog_color(b, 0x009000);
    klog_color("\n", 0x009000);
}

typedef enum { FS_KIND_CUSTOM = 0, FS_KIND_FAT32 = 1 } fs_kind_t;
static fs_kind_t g_fs_kind = FS_KIND_CUSTOM;

void cmd_format_custom(int argc, char** argv) {
    format_fs();
    g_fs_kind = FS_KIND_CUSTOM;
}

static uint32_t g_fat32_dir = 0;
static char g_fat32_path[128] = ">";

void cmd_mount32(int argc, char** argv) {
    uint32_t part_lba = 0;
    if (argc >= 2) part_lba = (uint32_t)strtol(argv[1], NULL, 0);
    select_drive(g_drives[g_current_drive].ata_base, g_drives[g_current_drive].is_slave);
    if (fat32_mount(part_lba) == 0) {
        g_fat32_dir = 0;
        strcpy(g_fat32_path, ">");
        g_fs_kind = FS_KIND_FAT32;
        klogf("FAT32 mounted (partition LBA %d)\n", part_lba);
        klogf("  bytes/sector: %d\n", g_fat32.bytes_per_sector);
        klogf("  sectors/cluster: %d\n", g_fat32.sectors_per_cluster);
        klogf("  root cluster: %d\n", g_fat32.root_cluster);
        klogf("  data clusters: %d\n", g_fat32.data_clusters);
    } else {
        kklog_color("No FAT32 filesystem found on this drive.", 0xFF0000);
    }
}

void cmd_format32(int argc, char** argv) {
    uint8_t spc = 0;
    if (argc >= 2) spc = (uint8_t)atoi(argv[1]);
    select_drive(g_drives[g_current_drive].ata_base, g_drives[g_current_drive].is_slave);
    uint32_t total_sectors = g_drives[g_current_drive].total_sectors;
    if (total_sectors == 0) {
        kklog_color("Drive has no known sector count. Run 'ld' first.", 0xFF0000);
        return;
    }
    kklog_color("FORMATTING AS FAT32...", 0x0000FF);
    if (fat32_format(0, total_sectors, spc) == 0) {
        g_fat32_dir = 0;
        strcpy(g_fat32_path, ">");
        g_fs_kind = FS_KIND_FAT32;
        klog_color("FAT32 format + mount successful", 0x00FF00);
    } else {
        kklog_color("FAT32 format failed (drive too small or bad params)", 0xFF0000);
    }
}

void cmd_ls32(int argc, char** argv) {
    if (!g_fat32.mounted) {
        kklog_color("No FAT32 volume mounted. Use 'mount32' first.", 0xFF0000);
        return;
    }
    fat32_dirent_t entries[64];
    int n = fat32_list_dir(g_fat32_dir, entries, 64);
    if (n < 0) {
        kklog("ls32: failed to read directory");
        return;
    }
    for (int i = 0; i < n; i++) {
        klog("  ");
        if (entries[i].attr & FAT32_ATTR_DIR) {
            klog_color(entries[i].name, 0xFF0000);
        } else {
            klog_color(entries[i].name, 0x00FF00);
        }
    }
    klog("\n");
}

void cmd_cd32(int argc, char** argv) {
    if (!g_fat32.mounted) {
        kklog_color("No FAT32 volume mounted. Use 'mount32' first.", 0xFF0000);
        return;
    }
    if (argc < 2) {
        klogf("Current FAT32 dir cluster: %d\n", g_fat32_dir);
        return;
    }
    if (strcmp(argv[1], "..") == 0) {
        g_fat32_dir = 0;
        strcpy(g_fat32_path, ">");
        return;
    }
    fat32_dirent_t entries[64];
    int n = fat32_list_dir(g_fat32_dir, entries, 64);
    int found = -1;
    for (int i = 0; i < n; i++) {
        if (strcasecmp(entries[i].name, argv[1]) == 0 && (entries[i].attr & FAT32_ATTR_DIR)) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        kklog("Error: Directory not found.");
        return;
    }
    g_fat32_dir = entries[found].first_cluster;
    strcat(g_fat32_path, argv[1]);
}

void cmd_read32(int argc, char** argv) {
    if (!g_fat32.mounted) {
        kklog_color("No FAT32 volume mounted. Use 'mount32' first.", 0xFF0000);
        return;
    }
    if (argc < 2) {
        kklog("Usage: read32 <filename>");
        return;
    }
    fat32_dirent_t entries[64];
    int n = fat32_list_dir(g_fat32_dir, entries, 64);
    int found = -1;
    for (int i = 0; i < n; i++) {
        if (strcasecmp(entries[i].name, argv[1]) == 0 && !(entries[i].attr & FAT32_ATTR_DIR)) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        kklog("Error: File not found");
        return;
    }
    static uint8_t buf[32768];
    uint32_t to_read = entries[found].size;
    if (to_read > sizeof(buf)) to_read = sizeof(buf);
    uint32_t bytes_read = fat32_read(&entries[found], 0, to_read, buf);
    if (bytes_read == 0) {
        kklog("Error: Could not read file (or empty)");
        return;
    }
    for (uint32_t i = 0; i < bytes_read; i++) {
        char ch[2] = {(char)buf[i], '\0'};
        vesa_print_string(ch);
    }
    vesa_print_string("\n");
}

void cmd_wr32(int argc, char** argv) {
    if (!g_fat32.mounted) {
        kklog_color("No FAT32 volume mounted. Use 'mount32' first.", 0xFF0000);
        return;
    }
    if (argc < 3) {
        kklog("Usage: wr32 <filename> <data>");
        return;
    }
    if (fat32_write_file(g_fat32_dir, argv[1], (const uint8_t*)argv[2], strlen(argv[2])) == 0) {
        kklog("write successful");
    } else {
        kklog("write failed");
    }
}

void cmd_ap32(int argc, char** argv) {
    if (!g_fat32.mounted) {
        kklog_color("No FAT32 volume mounted. Use 'mount32' first.", 0xFF0000);
        return;
    }
    if (argc < 3) {
        kklog("Usage: ap32 <filename> <data>");
        return;
    }
    if (fat32_append_file(g_fat32_dir, argv[1], (const uint8_t*)argv[2], strlen(argv[2])) == 0) {
        kklog("append successful");
    } else {
        kklog("append failed");
    }
}

void cmd_dl32(int argc, char** argv) {
    if (!g_fat32.mounted) {
        kklog_color("No FAT32 volume mounted. Use 'mount32' first.", 0xFF0000);
        return;
    }
    if (argc < 2) {
        kklog("usage: dl32 <file>");
        return;
    }
    if (fat32_delete_file(g_fat32_dir, argv[1]) == 0) {
        kklog("file deleted");
    } else {
        kklog("dl32: failed");
    }
}

void cmd_mf32(int argc, char** argv) {
    if (!g_fat32.mounted) {
        kklog_color("No FAT32 volume mounted. Use 'mount32' first.", 0xFF0000);
        return;
    }
    if (argc < 2) {
        kklog("Usage: mf32 <name>");
        return;
    }
    if (fat32_create_dir(g_fat32_dir, argv[1]) == 0) {
        kklog("directory created");
    } else {
        kklog("Failed to create directory.");
    }
}

void cmd_qformat(int argc, char** argv) {
    qformat_fs();
    g_fs_kind = FS_KIND_CUSTOM;
}

void cmd_usedisk(int argc, char** argv) {
    if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0' || argv[1][0] == ' ') {
        kklog("Usage: use <drive_index>\n       use <port> <0|1> (port: 0=primary,1=secondary or 0x1F0/0x170)\n");
        return;
    }
    if (argc == 2) {
        int drive_index = atoi(argv[1]);
        if (drive_index < 0 || drive_index >= g_active_drives) {
            kklog("Error: Invalid drive index.\n");
            return;
        }
        if (fs_change_drive(drive_index) == 0) {
            select_drive(g_drives[drive_index].ata_base, g_drives[drive_index].is_slave);
            klogf("Current drive now: %d", g_current_drive);
        } else {
            kklog("Error: Failed to change drive\n");
        }
        return;
    }
    const char* port_arg = argv[1];
    const char* unit_arg = argv[2];
    uint16_t base = 0;
    if (strcmp(port_arg, "0") == 0) base = 0x1F0;
    else if (strcmp(port_arg, "1") == 0) base = 0x170;
    else base = (uint16_t)strtol(port_arg, NULL, 0);
    int slave = 0;
    if (unit_arg[0] == '1' || strcasecmp(unit_arg, "slave") == 0 || strcasecmp(unit_arg, "s") == 0) slave = 1;
    int found = -1;
    for (int i = 0; i < g_active_drives; i++) {
        if (g_drives[i].ata_base == base && g_drives[i].is_slave == (uint8_t)slave) { found = i; break; }
    }
    if (found >= 0) {
        if (fs_change_drive(found) == 0) {
            select_drive(g_drives[found].ata_base, g_drives[found].is_slave);
            klogf("Switched to drive index %d\n", found);
            klogf("Current drive now: %d", g_current_drive);
        } else {
            kklog("Error: Failed to change drive\n");
        }
        return;
    }
    select_drive(base, (uint8_t)slave);
    if (g_active_drives < MAX_DRIVES) {
        int new_idx = g_active_drives;
        g_drives[new_idx].ata_base = base;
        g_drives[new_idx].is_slave = (uint8_t)slave;
        g_drives[new_idx].total_sectors = 0;
        g_current_drive = new_idx;
        g_active_drives++;
        init_fs();
        select_drive(g_drives[new_idx].ata_base, g_drives[new_idx].is_slave);
        g_current_drive = new_idx;
        klogf("Selected ATA base 0x%x slave %d (added as index %d)\n", (uint32_t)base, slave, g_current_drive);
    } else {
        klogf("Selected ATA base 0x%x slave %d\n", (uint32_t)base, slave);
    }
}

void cmd_find(int argc, char** argv) {
    if (argc < 2) {
        kklogf("usage: find <tag>\n");
        return;
    }
    const char* tag = argv[1];
    uint32_t results[32];
    int found_count = fs_find_by_tag(tag, results, 32);
    if (found_count <= 0) {
            kklogf("No files found with tag: %s", tag);
        } else {
            kklogf("Found %d files:", found_count);
        for (int i = 0; i < found_count; i++) {
            kklogf(" - Inode: %d", results[i]);
        }
    }
    klog("\n");
}

void cmd_app(int argc, char** argv) {
    if (argc < 2) {
        kklog("Usage: app <app_name>");
        return;
    }
    app(argv[1]);
}

void cmd_mf_custom(int argc, char** argv) {
    if (argc < 2) {
        kklog("Usage: mf <name>");
        return;
    }
    if (fs_create_dir(argv[1], g_current_dir) == -1) {
        kklog("Failed to create directory.");
    }
}

void cmd_cd_custom(int argc, char** argv) {
    if (argc < 2) {
        kklogf("Current directory inode: %d", g_current_dir);
        return;
    }

    int result = fs_cd(argv[1]);
    if (result == 0) {
        if (strcmp(argv[1], "..") == 0) {
            strcpy(g_current_path, ">");
        } else {
            strcpy(g_current_path, ">");
            strcat(g_current_path, argv[1]);
        }
    } else if (result == -2) {
        kklog("Error: Not a directory.");
    } else {
        kklog("Error: Directory not found.");
    }
}

void cmd_fs(int argc, char** argv) {
    if (argc < 2) {
        klogf("Active filesystem: %s\n", g_fs_kind == FS_KIND_FAT32 ? "fat32" : "custom");
        return;
    }
    if (strcasecmp(argv[1], "fat32") == 0) {
        if (!g_fat32.mounted) {
            kklog_color("No FAT32 volume mounted yet — use 'mount32' or 'format32' first.", 0xFF0000);
            return;
        }
        g_fs_kind = FS_KIND_FAT32;
        kklog("Switched to fat32");
    } else if (strcasecmp(argv[1], "custom") == 0) {
        g_fs_kind = FS_KIND_CUSTOM;
        kklog("Switched to custom");
    } else {
        kklog("Usage: fs <custom|fat32>");
    }
}

void cmd_format(int argc, char** argv) {
    if (argc >= 2 && strcasecmp(argv[1], "fat32") == 0) {
        cmd_format32(argc - 1, argv + 1);
        return;
    }
    cmd_format_custom(argc, argv);
}

void cmd_ls(int argc, char** argv) {
    if (g_fs_kind == FS_KIND_FAT32) cmd_ls32(argc, argv);
    else cmd_ls_custom(argc, argv);
}

void cmd_cd(int argc, char** argv) {
    if (g_fs_kind == FS_KIND_FAT32) cmd_cd32(argc, argv);
    else cmd_cd_custom(argc, argv);
}

void cmd_read(int argc, char** argv) {
    if (g_fs_kind == FS_KIND_FAT32) cmd_read32(argc, argv);
    else cmd_read_custom(argc, argv);
}

void cmd_wr(int argc, char** argv) {
    if (g_fs_kind == FS_KIND_FAT32) cmd_wr32(argc, argv);
    else cmd_wr_custom(argc, argv);
}

void cmd_dl(int argc, char** argv) {
    if (g_fs_kind == FS_KIND_FAT32) cmd_dl32(argc, argv);
    else cmd_dl_custom(argc, argv);
}

void cmd_mf(int argc, char** argv) {
    if (g_fs_kind == FS_KIND_FAT32) cmd_mf32(argc, argv);
    else cmd_mf_custom(argc, argv);
}

void cmd_usb(int argc, char** argv) {
    int count = usb_device_count();
    if (count == 0) {
        kklog_color("No USB devices connected.", 0xFF0000);
        return;
    }
    klogf_color("Connected USB devices: %d\n", 0x00FF00, count);
    for (int i = 0; i < count; i++) {
        usb_device_t* dev = usb_get_device(i);
        if (!dev) continue;
        klog("\n");
        klogf_color("Device %d\n", 0xFFFF00, i);
        klogf("  Address:      %d\n", dev->address);
        klogf("  Speed:        %s\n", usb_speed_str(dev->speed));
        klogf("  Vendor ID:    0x%x\n", dev->dev_desc.idVendor);
        klogf("  Product ID:   0x%x\n", dev->dev_desc.idProduct);
        klogf("  USB Version:  0x%x\n", dev->dev_desc.bcdUSB);
        klogf("  Class:        %s (0x%x)\n", usb_class_str(dev->iface_class), dev->iface_class);
        klogf("  Subclass:     0x%x\n", dev->iface_subclass);
        klogf("  Protocol:     0x%x\n", dev->iface_protocol);
        if (dev->hub_addr == 0) {
            klogf("  Attached to:  root hub, port %d\n", dev->hub_port + 1);
        } else {
            klogf("  Attached to:  hub @addr %d, port %d\n", dev->hub_addr, dev->hub_port + 1);
        }
        if (dev->ep_in_addr != 0) {
            klogf("  IN Endpoint:  0x%x (maxpkt %d, interval %d)\n", dev->ep_in_addr, dev->ep_in_maxpkt, dev->ep_in_interval);
        }
    }
}

void cmd_open(int argc, char** argv) {
    if (argc < 2) {
        kklogf("usage: open <file>\n");
        return;
    }
    char* filename = argv[1];
    char* dot = strrchr(filename, '.');
    if (dot != NULL) {
        if (strcmp(dot, ".wav") == 0) {
            play_wav_file_jmp(filename);
        } 
        else if (strcmp(dot, ".txt") == 0) {
            cmd_read(argc, argv);
        } 
        else if (strcmp(dot, ".tpl") == 0) {
            templar_run_file(filename);
        } 
        else {
            klog_status("UNKNOWN EXTENSION", 0xFF0000);
        }
    } else {
        klog_status("NO EXTENSION", 0xFF0000);
    }
}

void cmd_ac97_set_volume(int argc, char** argv) {
    if (argc < 2) {
        kklog("Usage: vol <0-31>\n");
        return;
    }
    int val = atoi(argv[1]);
    if (val < 0) val = 0;
    if (val > 31) val = 31;
    uint8_t vol = (uint8_t)val;
    ac97_set_volume(vol);
}
uint8_t timezone = 0;

void cmd_set_timezone(int argc, char** argv) {
    if (argc < 2) {
        kklog("Usage: sett <0-12>\n");
        return;
    }
    int val = atoi(argv[1]);
    timezone = (uint8_t)val;
}

command_t commands[] = {
    {"help", cmd_help},
    {"clear", cmd_clear},
    {"reboot", cmd_reboot},
    {"cow", cmd_cow},
    {"cat", cmd_cat},
    {"ld", cmd_ld},
    {"fd", cmd_find},
    {"read", cmd_read},
    {"ls", cmd_ls},
    {"lib", library},
    {"wr", cmd_wr},
    {"dl", cmd_dl},
    {"time", cmd_time},
    {"format", cmd_format},
    {"fs", cmd_fs},
    {"format32", cmd_format32},
    {"mount32", cmd_mount32},
    {"qformat", cmd_qformat},
    {"use", cmd_usedisk},
    {"mem", cmd_mem},
    {"shutdown", cmd_shutdown},
    {"app", cmd_app},
    {"mf", cmd_mf},
    {"cd", cmd_cd},
    {"usb", cmd_usb},
    {"open", cmd_open},
    {"mustop", ac97_stop},
    {"mupause", ac97_pause},
    {"vol", cmd_ac97_set_volume},
    {"sett", cmd_set_timezone},
};

int command_count = sizeof(commands)/sizeof(command_t);