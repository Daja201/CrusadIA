#include "commands.h"
#include "terminal.h"
#include "string.h"
#include <stdint.h>
#include "reboot.h"
#include "diskinfo.h"
#include "vfs.h"
#include "fcntl.h"
#include "library.h"
#include "klog.h"
#include "string.h"
#include "rtc.h"
#include "vesa.h"
#include "pmm.h"
#include "pci.h"
#include "app.h"
#include "ac97.h"
#include "speaker.h"
#include "task.h"
#include "usb.h"
#include "usbhid.h"
#include "pl2303.h"
#include "pump.h"
#include "serial.h"
#include "tccport/tcc_kernel.h"

#define CHUNK_SIZE 65532
void cmd_cd(int argc, char** argv);
void cmd_dl(int argc, char** argv);
void cmd_mf(int argc, char** argv);
void cmd_wr(int argc, char** argv);
void cmd_umount(int argc, char** argv);
void cmd_setroot(int argc, char** argv);
extern uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
extern void pci_config_write(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);

static uint8_t wav_audio_buffer[65536] __attribute__((aligned(8)));
static uint8_t streaming_buffer[CHUNK_SIZE] __attribute__((aligned(8)));

struct ac97_bdl_entry {
    uint32_t buffer_addr;
    uint16_t length;
    uint16_t flags;
} __attribute__((packed));

static struct ac97_bdl_entry bdl[1] __attribute__((aligned(8)));
static int16_t audio_buffer[32000];

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

void cmd_mem(int argc, char** argv) {
    (void)argc;
    (void)argv;
    uint32_t free_kb = pmm_count_mem();
    klogf("Free memory: %d KB (%d MB)\n", free_kb, free_kb / 1024);
}

static void cmd_ac97_stop(int argc, char** argv) {
    (void)argc;
    (void)argv;
    ac97_stop();
}

static void cmd_ac97_pause(int argc, char** argv) {
    (void)argc;
    (void)argv;
    ac97_pause();
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
    (void)argc;
    (void)argv;
    int count = vfs_drive_count();
    if (count <= 0) {
        kklog_color("No drives detected.", 0xFF0000);
        return;
    }
    int mounts = vfs_mount_count();
    for (int i = 0; i < count; i++) {
        int fs = vfs_detect(i);
        klogf("d%d.0  %d MB  %s\n", i, (int)vfs_drive_size_mb(i), fs == VFS_FS_NONE ? "no filesystem" : vfs_fs_name(fs));
        for (int j = 0; j < mounts; j++) {
            vfs_mount_info_t info;
            if (vfs_mount_get(j, &info) != 0 || info.drive != i) continue;
            klogf("    mounted at %s%s\n", info.mountpoint, info.is_root ? " (root)" : "");
        }
    }
}

void cmd_clear(int argc, char** argv) {
    vesa_clear(0x000000);
}

void cmd_reboot(int argc, char** argv) {
    kklog("reboot in process\n");
    reboot_triple_fault();

}

void cmd_time(int argc, char** argv) {
    int year, month, day;
    int hour, min, sec;
    rtc_get_datetime(&year, &month, &day, &hour, &min, &sec);
    char b[8];
    klog_color("RTC: ", 0x009000);
    itoa(year, b, 10); klog_color(b, 0x009000); itoa(month, b, 10); klog_color(" ", 0x009000); klog_color(b, 0x009000); itoa(day, b, 10); klog_color(" ", 0x009000); klog_color(b, 0x009000); klog_color(" ", 0x009000);
    itoa(hour, b, 10); klog_color(b, 0x009000); klog_color(":", 0x009000);
    itoa(min, b, 10); klog_color(b, 0x009000); klog_color(":", 0x009000);
    itoa(sec, b, 10); klog_color(b, 0x009000);
    klog_color("\n", 0x009000);
}

void cmd_shutdown(int argc, char** argv) {
    kklog_color("\nPREPARING FS...\n", 0xFF0000);
    vfs_shutdown();
    busy_ms(1000);
    vesa_draw_rec(0, 0, 1920, 1080, 0x000000);
    const char* verse = "When you lie down, you will not be afraid. Yes, you will lie down, and your sleep will be sweet. (PROVERBS 3:24)";
    const char* msg = "IT IS NOW SAFE TO TURN OFF YOUR COMPUTER. GOOD NIGHT :)";
    int start_x_msg = (1920 - (strlen(msg) * 8)) / 2;
    int start_y_msg = 350;
    int start_x_verse = (1920 - (strlen(verse) * 8)) / 2;
    int start_y_verse = start_y_msg - 20;
    for (int i = 0; verse[i] != '\0'; i++) {
        vesa_draw_char(verse[i], start_x_verse + (i * 8), start_y_verse, 0x2BC7FB, 0x000000);
    }
    for (int i = 0; msg[i] != '\0'; i++) {
        vesa_draw_char(msg[i], start_x_msg + (i * 8), start_y_msg, 0x2BC7FB, 0x000000);
    }
    vesa_swap();
    for (;;) asm volatile ("pause");
}

#define FMT_MAX_MOUNTS 8

static int is_fs_name(const char* s) {
    return strcasecmp(s, "fat32") == 0 || strcasecmp(s, "fat") == 0 ||
           strcasecmp(s, "cos") == 0 || strcasecmp(s, "cosfs") == 0;
}

static int drive_mounts(int drive, char paths[][VFS_PATH_MAX], int max) {
    int found = 0;
    int count = vfs_mount_count();
    for (int i = 0; i < count && found < max; i++) {
        vfs_mount_info_t info;
        if (vfs_mount_get(i, &info) == 0 && info.drive == drive) {
            strcpy(paths[found], info.mountpoint);
            found++;
        }
    }
    return found;
}

static int root_mounted(void) {
    int count = vfs_mount_count();
    for (int i = 0; i < count; i++) {
        vfs_mount_info_t info;
        if (vfs_mount_get(i, &info) == 0 && info.is_root) return 1;
    }
    return 0;
}

static void drive_label(int drive, char* out) {
    out[0] = 'd';
    itoa(drive, out + 1, 10);
}

static int format_drive(const char* input, const char* fsname) {
    int drive = vfs_parse_drive(input);
    if (drive < 0) return VFS_E_NODRIVE;
    char label[8];
    drive_label(drive, label);
    char paths[FMT_MAX_MOUNTS][VFS_PATH_MAX];
    int n = drive_mounts(drive, paths, FMT_MAX_MOUNTS);
    int r = 0;
    for (int i = n - 1; i >= 0 && r == 0; i--) {
        if (strcmp(paths[i], "/") == 0) continue;
        r = vfs_umount(paths[i]);
    }
    for (int i = 0; i < n && r == 0; i++) {
        if (strcmp(paths[i], "/") != 0) continue;
        r = vfs_umount("/");
    }
    if (r == 0) r = vfs_format(label, fsname);
    if (n == 0 && r == 0) {
        strcpy(paths[0], root_mounted() ? "/mount/" : "/");
        if (paths[0][1] != '\0') strcat(paths[0], label);
        n = 1;
    }
    for (int i = 0; i < n; i++) {
        if (strcmp(paths[i], "/") != 0) continue;
        int rr = vfs_mount(label, "/");
        if (r == 0 && rr != 0) r = rr;
    }
    for (int i = 0; i < n; i++) {
        if (strcmp(paths[i], "/") == 0) continue;
        int rr = vfs_mount(label, paths[i]);
        if (r == 0 && rr != 0) r = rr;
    }
    return r;
}

static void list_directory(const char* path) {
    vfs_dirent_t entries[64];
    int count = vfs_readdir(path, entries, 64);
    if (count < 0) {
        kklogf("ls: %s", vfs_strerror(count));
        return;
    }
    for (int i = 0; i < count; i++) {
        klog("  ");
        klog_color(entries[i].name, entries[i].is_dir ? 0xFF0000 : 0x00FF00);
        klog("\n");
    }
}

static void read_file(const char* path) {
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        kklogf("read: %s", vfs_strerror(fd));
        return;
    }
    static uint8_t buf[32768];
    long size = vfs_fsize(fd);
    uint32_t count = size > (long)sizeof(buf) ? sizeof(buf) : (size > 0 ? (uint32_t)size : 0);
    long bytes = count ? vfs_read(fd, buf, count) : 0;
    vfs_close(fd);
    if (bytes <= 0) {
        kklog("Error: Could not read file (or file is empty)");
        return;
    }
    for (long i = 0; i < bytes && buf[i] != '\0'; i++) {
        char ch[2] = {(char)buf[i], '\0'};
        vesa_print_string(ch);
    }
    vesa_print_string("\n");
}

void cmd_setroot(int argc, char **argv) {
    if (argc < 2) {
        kklog("Usage: setroot <drive>");
        return;
    }
    int r = vfs_mount(argv[1], "/");
    if (r != 0) {
        kklogf("setroot: %s", vfs_strerror(r));
    } else {
        kklogf("Root set to %s", argv[1]);
    }
}

void cmd_mount(int argc, char** argv) {
    if (argc < 3) {
        kklog("Usage: mount <drive> <location>");
        return;
    }
    int r = vfs_mount(argv[1], argv[2]);
    if (r != 0) {
        kklogf("mount: %s", vfs_strerror(r));
        return;
    }
    kklogf("Mounted %s at %s", argv[1], argv[2]);
}

void cmd_umount(int argc, char** argv) {
    if (argc < 2) {
        kklog("Usage: umount <drive|location>");
        return;
    }
    int r = vfs_umount(argv[1]);
    if (r != 0) kklogf("umount: %s", vfs_strerror(r));
    else kklogf("Unmounted %s", argv[1]);
}

void cmd_usedisk(int argc, char** argv) {
    if (argc < 2) {
        kklog("Usage: use <drive>");
        return;
    }
    int drive = vfs_parse_drive(argv[1]);
    if (drive < 0) {
        kklogf("use: %s", vfs_strerror(VFS_E_NODRIVE));
        return;
    }
    char label[8];
    drive_label(drive, label);
    char paths[1][VFS_PATH_MAX];
    int r = 0;
    if (drive_mounts(drive, paths, 1) == 0) {
        strcpy(paths[0], "/mount/");
        strcat(paths[0], label);
        r = vfs_mount(label, paths[0]);
    }
    if (r == 0) r = vfs_chdir(paths[0]);
    if (r != 0) kklogf("use: %s", vfs_strerror(r));
}

void cmd_find(int argc, char** argv) {
    if (argc < 2) {
        kklogf("usage: find <tag>\n");
        return;
    }
    uint32_t results[32];
    int found_count = vfs_find_by_tag(argv[1], results, 32);
    if (found_count < 0) {
        kklogf("find: %s", vfs_strerror(found_count));
        return;
    }
    if (found_count == 0) kklogf("No files found with tag: %s", argv[1]);
    else {
        kklogf("Found %d files:", found_count);
        for (int i = 0; i < found_count; i++) kklogf(" - Inode: %d", results[i]);
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

void cmd_fs(int argc, char** argv) {
    (void)argc;
    (void)argv;
    int count = vfs_mount_count();
    for (int i = 0; i < count; i++) {
        vfs_mount_info_t info;
        if (vfs_mount_get(i, &info) == 0)
            klogf("%s at %s on d%d%s\n", vfs_fs_name(info.fs), info.mountpoint, info.drive, info.is_root ? " [root]" : "");
    }
    if (!count) kklog("No filesystems are mounted.");
}

void cmd_format(int argc, char** argv) {
    const char* drive = 0;
    const char* fsname = 0;
    for (int i = 1; i < argc; i++) {
        if (is_fs_name(argv[i])) fsname = argv[i];
        else drive = argv[i];
    }
    if (!drive || !fsname) {
        kklog("Usage: format <drive> <fat32|cos>");
        return;
    }
    kklog_color("FORMATTING...", 0xFFFF00);
    int r = format_drive(drive, fsname);
    if (r != 0) kklogf("format: %s", vfs_strerror(r));
    else kklogf("%s formatted", drive);
}

void cmd_ls(int argc, char** argv) {
    list_directory(argc >= 2 ? argv[1] : ".");
}

void cmd_cd(int argc, char** argv) {
    if (argc < 2) {
        kklogf("%s", vfs_getcwd());
        return;
    }
    int r = vfs_chdir(argv[1]);
    if (r != 0) kklogf("cd: %s", vfs_strerror(r));
}

void cmd_read(int argc, char** argv) {
    if (argc < 2) {
        kklog("Usage: read <filename>");
        return;
    }
    read_file(argv[1]);
}

void cmd_wr(int argc, char** argv) {
    if (argc < 3) {
        kklog("Usage: wr <filename> <data>");
        return;
    }
    long r = vfs_write_file(argv[1], argv[2], strlen(argv[2]), "wr", 0);
    if (r < 0) kklogf("write: %s", vfs_strerror((int)r));
    else kklog("write successful");
}

void cmd_dl(int argc, char** argv) {
    if (argc < 2) {
        kklog("Usage: dl <file>");
        return;
    }
    vfs_stat_t st;
    int r = vfs_stat(argv[1], &st);
    if (r != 0 || st.is_dir) {
        kklog("dl: file not found or path is a directory");
        return;
    }
    r = vfs_remove(argv[1]);
    if (r != 0) kklogf("dl: %s", vfs_strerror(r));
    else kklog("file deleted");
}

void cmd_mf(int argc, char** argv) {
    if (argc < 2) {
        kklog("Usage: mf <name>");
        return;
    }
    int r = vfs_mkdir(argv[1]);
    if (r != 0) kklogf("mkdir: %s", vfs_strerror(r));
    else kklog("directory created");
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

#define BMP_MAX_ROW_BYTES 8192

static int bmp_parse_header(uint8_t* hdr, uint32_t* data_off, int32_t* width, int32_t* height, uint16_t* bpp, uint32_t* comp) {
    if (hdr[0] != 'B' || hdr[1] != 'M') return -1;
    memcpy(data_off, hdr + 10, 4);
    memcpy(width, hdr + 18, 4);
    memcpy(height, hdr + 22, 4);
    memcpy(bpp, hdr + 28, 2);
    memcpy(comp, hdr + 30, 4);
    if (*bpp != 24 || *comp != 0) return -2;
    return 0;
}

static void showimage_file(const char* path) {
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        kklogf("showimage: %s", vfs_strerror(fd));
        return;
    }
    uint8_t hdr[54];
    if (vfs_pread(fd, 0, hdr, sizeof(hdr)) < (long)sizeof(hdr)) {
        vfs_close(fd);
        klog_status("ERROR COULD NOT READ BMP HEADER", 0xFF0000);
        return;
    }
    uint32_t data_off, comp;
    int32_t width, height;
    uint16_t bpp;
    int rc = bmp_parse_header(hdr, &data_off, &width, &height, &bpp, &comp);
    if (rc == -1) {
        vfs_close(fd);
        klog_status("NOT A VALID BMP FILE", 0xFF0000);
        return;
    } else if (rc == -2) {
        vfs_close(fd);
        klog_status("UNSUPPORTED BMP FORMAT", 0xFF0000);
        return;
    }
    if (width <= 0 || height == 0) {
        vfs_close(fd);
        klog_status("INVALID BMP DIMENSIONS", 0xFF0000);
        return;
    }
    int top_down = height < 0;
    uint32_t abs_height = top_down ? (uint32_t)(-height) : (uint32_t)height;
    uint32_t row_size = ((uint32_t)width * 3 + 3) & ~3u;
    static uint8_t row[BMP_MAX_ROW_BYTES];
    if (row_size > sizeof(row)) {
        vfs_close(fd);
        klog_status("BMP TOO WIDE", 0xFF0000);
        return;
    }
    int origin_x = c_x;
    int origin_y = c_y;
    for (uint32_t r = 0; r < abs_height; r++) {
        uint32_t file_row = top_down ? r : (abs_height - 1 - r);
        uint32_t offset = data_off + file_row * row_size;
        if (vfs_pread(fd, offset, row, row_size) < (long)row_size) break;
        for (int32_t x = 0; x < width; x++) {
            uint8_t b = row[x * 3];
            uint8_t g = row[x * 3 + 1];
            uint8_t rr = row[x * 3 + 2];
            uint32_t color = ((uint32_t)rr << 16) | ((uint32_t)g << 8) | b;
            vesa_putpixel(origin_x + x, origin_y + (int)r, color);
        }
    }
    vesa_swap();
    vfs_close(fd);
}

void cmd_showimage(int argc, char** argv) {
    if (argc < 2) {
        kklog("Usage: showimage <filename>");
        return;
    }
    showimage_file(argv[1]);
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
        else if (strcmp(dot, ".cap") == 0) {
            cmd_cc(argc, argv);
        } 
        else if (strcmp(dot, ".bmp") == 0) {
            cmd_showimage(argc, argv);
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

void cmd_send_serial(int argc, char** argv) {
    if (argc < 2) {
        kklog("Usage: ss <string>\n");
        return;
    }
    for (char *p = argv[1]; *p; p++) {
        write_serial(*p);
    }
}

static void pump_report(const char* label, int r, const char* reply) {
    if (r < 0) {
        klog_status("PUMP: NO REPLY (check adapter / serial enable link)", 0xFF0000);
        return;
    }
    kklogf("%s -> %s\n", label, reply);
}

void cmd_pumpon(int argc, char** argv) {
    (void)argc; (void)argv;
    if (!pump_is_ready()) { klog_status("PUMP: NO USB-SERIAL ADAPTER ATTACHED", 0xFF0000); return; }
    char reply[PUMP_MAX_MSG];
    int r = pump_start(reply, sizeof(reply));
    pump_report("start", r, reply);
}

void cmd_pumpoff(int argc, char** argv) {
    (void)argc; (void)argv;
    if (!pump_is_ready()) { klog_status("PUMP: NO USB-SERIAL ADAPTER ATTACHED", 0xFF0000); return; }
    char reply[PUMP_MAX_MSG];
    int r = pump_stop(reply, sizeof(reply));
    pump_report("stop", r, reply);
}

void cmd_pumpspeed(int argc, char** argv) {
    (void)argc; (void)argv;
    if (!pump_is_ready()) { klog_status("PUMP: NO USB-SERIAL ADAPTER ATTACHED", 0xFF0000); return; }
    char reply[PUMP_MAX_MSG];
    int r = pump_query_speed(reply, sizeof(reply));
    pump_report("speed", r, reply);
}

void cmd_pumpfull(int argc, char** argv) {
    (void)argc; (void)argv;
    if (!pump_is_ready()) { klog_status("PUMP: NO USB-SERIAL ADAPTER ATTACHED", 0xFF0000); return; }
    char reply[PUMP_MAX_MSG];
    int r = pump_target_full_speed(reply, sizeof(reply));
    pump_report("target-full", r, reply);
}

void cmd_pumpstandby(int argc, char** argv) {
    (void)argc; (void)argv;
    if (!pump_is_ready()) { klog_status("PUMP: NO USB-SERIAL ADAPTER ATTACHED", 0xFF0000); return; }
    char reply[PUMP_MAX_MSG];
    int r = pump_target_standby_speed(reply, sizeof(reply));
    pump_report("target-standby", r, reply);
}

void cmd_pumptype(int argc, char** argv) {
    (void)argc; (void)argv;
    if (!pump_is_ready()) { klog_status("PUMP: NO USB-SERIAL ADAPTER ATTACHED", 0xFF0000); return; }
    char reply[PUMP_MAX_MSG];
    int r = pump_query_pump_type(reply, sizeof(reply));
    pump_report("type", r, reply);
}

void cmd_pumpcmd(int argc, char** argv) {
    if (argc < 3) {
        kklog("Usage: pumpcmd <!|?> <BODY, e.g. C852 1>\n");
        return;
    }
    if (!pump_is_ready()) { klog_status("PUMP: NO USB-SERIAL ADAPTER ATTACHED", 0xFF0000); return; }
    char start = argv[1][0];
    if (start != '!' && start != '?') {
        kklog("First arg must be '!' (store/command) or '?' (query)\n");
        return;
    }
    char reply[PUMP_MAX_MSG];
    int r = pump_send_command(start, argv[2], reply, sizeof(reply));
    pump_report("pumpcmd", r, reply);
}

extern volatile uint32_t system_ticks;

static void delay_ms(uint32_t ms) {
    uint32_t target = system_ticks + ms;
    while (system_ticks < target) {
        asm volatile("hlt");
    }
}

void cmd_blink(int argc, char** argv) {
    usb_device_t* dev = pl2303_get_active();
    if (!dev) { klog_status("BLINK: NO USB-SERIAL ADAPTER ATTACHED", 0xFF0000); return; }

    int times = (argc >= 2) ? atoi(argv[1]) : 5;
    int period_ms = (argc >= 3) ? atoi(argv[2]) : 500;
    if (times <= 0) times = 5;
    if (period_ms <= 0) period_ms = 500;

    kklogf("blink: toggling DTR x%d (%dms period)\n", times, period_ms);
    pl2303_set_control_lines(dev, 1, 0);

}

void cmd_debug(int argc, char** argv) {
    vesa_clear(0x000000);
    klog("MOUSE X:");
    kklogf("%d", mouse_x);
    klog("MOUSE Y:");
    kklogf("%d", mouse_y);
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
    {"mount", cmd_mount},
    {"umount", cmd_umount},
    {"setroot", cmd_setroot},
    {"use", cmd_usedisk},
    {"mem", cmd_mem},
    {"shutdown", cmd_shutdown},
    {"app", cmd_app},
    {"mf", cmd_mf},
    {"cd", cmd_cd},
    {"usb", cmd_usb},
    {"open", cmd_open},
    {"mustop", cmd_ac97_stop},
    {"mupause", cmd_ac97_pause},
    {"vol", cmd_ac97_set_volume},
    {"sett", cmd_set_timezone},
    {"ss", cmd_send_serial},
    {"pumpon", cmd_pumpon},
    {"pumpoff", cmd_pumpoff},
    {"pumpspeed", cmd_pumpspeed},
    {"pumpfull", cmd_pumpfull},
    {"pumpstandby", cmd_pumpstandby},
    {"pumptype", cmd_pumptype},
    {"pumpcmd", cmd_pumpcmd},
    {"blink", cmd_blink},
    {"debug", cmd_debug},
    {"cc", cmd_cc},
    {"showimage", cmd_showimage},
};

int command_count = sizeof(commands)/sizeof(command_t);