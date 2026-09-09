//AC97 sound cards driver

#include "ac97.h"
#include "pci.h"
#include "klog.h"
#include "io.h"
#include "fs.h"
#include "task.h"
#include <string.h>
#include <stdint.h>

#define CHUNK_SIZE 65532 //audio buffer
#define CHUNK_SECTORS 256 //how many storage sectors need to be read from drive (wav, not whole waw at once)
#define BDL_ENTRIES 32 //based on ac97 specs

extern void select_drive(uint16_t base, uint8_t slave);
extern void block_read(uint32_t lba, uint8_t* buf);

extern uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
extern void pci_config_write(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
pci_device_t g_dev;
extern uint32_t g_current_dir;
extern void read_inode(int idx, inode_t* inode);
extern int fs_resolve_path(const char* path, uint32_t current_dir_inode);

struct ac97_bdl_entry {
    uint32_t buffer_addr;
    uint16_t length;
    uint16_t flags;
} __attribute__((packed));

static struct ac97_bdl_entry bdl[BDL_ENTRIES] __attribute__((aligned(8)));
static uint8_t stream_bufs[BDL_ENTRIES][CHUNK_SIZE] __attribute__((aligned(8)));
static int16_t audio_buffer[32000];

static volatile int g_ac97_stop_requested = 0;
static volatile int g_ac97_paused = 0;
static uint8_t g_ac97_vol_atten = 0; 
static uint8_t g_ac97_muted = 0;

#define AC97_NAM_PCM_OUT_VOL   0x18
#define AC97_NAM_EXT_AUDIO_ID  0x28
#define AC97_NAM_EXT_AUDIO_CTL 0x2A
#define AC97_NAM_FRONT_DAC_RATE 0x2C
#define AC97_EA_VRA            0x0001

void ac97_stop(void) {
    g_ac97_stop_requested = 1;
    g_ac97_paused = 0;
}

void ac97_pause(void) {
    if(g_ac97_paused == 0) {
        g_ac97_paused = 1;
    } else {
        g_ac97_paused = 0;
    }
}

void ac97_set_volume(uint8_t vol_atten) {
    if (vol_atten > 31) vol_atten = 31;
    g_ac97_vol_atten = vol_atten;
    uint32_t real_bar0 = pci_config_read(g_dev.bus, g_dev.device, g_dev.function, 0x10);
    uint16_t nam_port = (uint16_t)(real_bar0 & ~0x3);
    uint16_t val = (uint16_t)((vol_atten << 8) | vol_atten);
    if (g_ac97_muted) val |= 0x8000;
    outw(nam_port + AC97_NAM_PCM_OUT_VOL, val);
}

void ac97_set_mute(int mute) {
    g_ac97_muted = mute ? 1 : 0;
    ac97_set_volume(g_ac97_vol_atten);
}

static uint32_t ac97_set_rate(uint16_t nam_port, uint32_t rate) {
    uint16_t ext_id = inw(nam_port + AC97_NAM_EXT_AUDIO_ID);
    if (!(ext_id & AC97_EA_VRA)) {
        return 48000;
    }
    uint16_t ctl = inw(nam_port + AC97_NAM_EXT_AUDIO_CTL);
    outw(nam_port + AC97_NAM_EXT_AUDIO_CTL, ctl | AC97_EA_VRA);
    if (rate < 8000) rate = 8000;
    if (rate > 48000) rate = 48000;
    outw(nam_port + AC97_NAM_FRONT_DAC_RATE, (uint16_t)rate);
    return inw(nam_port + AC97_NAM_FRONT_DAC_RATE);
}

static int ac97_wait_reset_done(uint16_t nabm_port) {
    for (int i = 0; i < 100000; i++) {
        if ((inb(nabm_port + 0x1B) & 0x02) == 0) {
            return 0;
        }
        asm volatile("pause");
    }
    return -1;
}

static uint16_t ac97_get_nabm_port(void) {
    uint32_t real_bar1 = pci_config_read(g_dev.bus, g_dev.device, g_dev.function, 0x14);
    return (uint16_t)(real_bar1 & ~0x3);
}

static void ac97_enable_device(void) {
    uint32_t cmd = pci_config_read(g_dev.bus, g_dev.device, g_dev.function, 0x04);
    if ((cmd & 0x05) != 0x05) {
        pci_config_write(g_dev.bus, g_dev.device, g_dev.function, 0x04, cmd | 0x05);
    }
}

static int ac97_reset_stream(uint16_t nabm_port) {
    ac97_enable_device();
    outb(nabm_port + 0x1B, 0x02);
    if (ac97_wait_reset_done(nabm_port) != 0) {
        kklog("AC97: reset timed out\n");
        return -1;
    }
    outw(nabm_port + 0x16, 0x1C);
    return 0;
}

int ac97_init(void) {
    if (g_dev.vendor_id != 0) {
        return 0;
    }
    if (pci_find_class(0x04, 0x01, &g_dev, 0) != 0) {
        kklog("NO AC97 DEVICE");
        return -1;
    }
    return 0;
}

int ac97_play_pcm(void* buffer, uint32_t length_bytes) {
    if (length_bytes < 4) {
        return 0;
    }
    if (ac97_init() != 0) {
        return -1;
    }
    uint16_t nabm_port = ac97_get_nabm_port();
    if (ac97_reset_stream(nabm_port) != 0) {
        return -1;
    }
    bdl[0].buffer_addr = (uint32_t)buffer;
    bdl[0].length = length_bytes / 2;
    bdl[0].flags = 0x8000;
    outl(nabm_port + 0x10, (uint32_t)&bdl);
    outb(nabm_port + 0x15, 0);
    outb(nabm_port + 0x1B, 0x01);
    uint32_t spins = 0;
    while (!(inw(nabm_port + 0x16) & 0x1C)) {
        if (++spins > 5000000) {
            kklog("AC97: buffer completion timed out\n");
            outb(nabm_port + 0x1B, 0x00);
            return -1;
        }
        asm volatile("pause");
    }
    outw(nabm_port + 0x16, 0x1C);
    outb(nabm_port + 0x1B, 0x00);
    return 0;
}

int prep_play(void){
    if (ac97_init() != 0) {
        return -1;
    }
    uint32_t cmd = pci_config_read(g_dev.bus, g_dev.device, g_dev.function, 0x04);
    pci_config_write(g_dev.bus, g_dev.device, g_dev.function, 0x04, cmd | 0x05);
    return 0;
}

int ac97_play_test_tone(void) {
    if (g_dev.vendor_id == 0) {
        kklog("NO AC97 DEVICE");
        return -1;
    }
    uint32_t cmd = pci_config_read(g_dev.bus, g_dev.device, g_dev.function, 0x04);
    pci_config_write(g_dev.bus, g_dev.device, g_dev.function, 0x04, cmd | 0x05);
    uint32_t real_bar0 = pci_config_read(g_dev.bus, g_dev.device, g_dev.function, 0x10);
    uint32_t real_bar1 = pci_config_read(g_dev.bus, g_dev.device, g_dev.function, 0x14);
    uint16_t nam_port = real_bar0 & ~0x3;
    uint16_t nabm_port = real_bar1 & ~0x3;
    if (nam_port == 0 || nabm_port == 0) {
        return -1;
    }
    outw(nam_port + 0x00, 1);
    outw(nam_port + 0x02, 0x0000);
    outw(nam_port + 0x18, 0x0000);
    int period = 54;
    int16_t volume = 2000;
    for (int i = 0; i < 32000; i += 2) {
        if ((i / 2) % period < (period / 2)) {
            audio_buffer[i] = volume; audio_buffer[i+1] = volume;
        } else {
            audio_buffer[i] = -volume; audio_buffer[i+1] = -volume;
        }
    }
    outb(nabm_port + 0x1B, 0x02);
    if (ac97_wait_reset_done(nabm_port) != 0) {
        kklog("AC97: reset timed out\n");
        return -1;
    }
    outw(nabm_port + 0x16, 0x1C);
    bdl[0].buffer_addr = (uint32_t)&audio_buffer;
    bdl[0].length = 32000;
    bdl[0].flags = 0x8000;
    outl(nabm_port + 0x10, (uint32_t)&bdl);
    outb(nabm_port + 0x15, 0);
    outb(nabm_port + 0x1B, 0x01);
    while ((inw(nabm_port + 0x16) & 0x08) == 0) {
        asm volatile("pause");
    }
    outw(nabm_port + 0x16, 0x08);
    outb(nabm_port + 0x1B, 0x00);
    kklog("BEEP");
    return 0;
}

static char g_pending_wav_path[128];
static void play_wav_file_entry(void) {
    play_wav_file(g_pending_wav_path);
}
void play_wav_file_jmp(const char* filename) {
    strncpy(g_pending_wav_path, filename, sizeof(g_pending_wav_path) - 1);
    g_pending_wav_path[sizeof(g_pending_wav_path) - 1] = '\0';
    create_task(play_wav_file_entry);
}

int play_wav_file(const char* filename) {
    ac97_play_test_tone();
    if (ac97_init() != 0) {
        return -1;
    }
    int inode_num = fs_resolve_path(filename, g_current_dir);
    if (inode_num < 0) {
        kklogf_color("WAV: %s not found\n", 0x00FF00, filename);
        return -1;
    }
    inode_t file_node;
    read_inode(inode_num, &file_node);

    uint8_t riff_hdr[12];
    fs_read((uint32_t)inode_num, &file_node, 0, sizeof(riff_hdr), riff_hdr);
    if (riff_hdr[0] != 'R' || riff_hdr[1] != 'I' || riff_hdr[2] != 'F' || riff_hdr[3] != 'F' ||
        riff_hdr[8] != 'W' || riff_hdr[9] != 'A' || riff_hdr[10] != 'V' || riff_hdr[11] != 'E') {
        kklog("WAV: Invalid format\n");
        return -1;
    }

    uint32_t chunk_offset = 12;
    uint32_t data_offset = 0;
    uint32_t total_data = 0;
    uint32_t wav_sample_rate = 48000;
    uint16_t wav_channels = 2;
    while (chunk_offset + 8 <= file_node.size) {
        uint8_t chunk_hdr[8];
        fs_read((uint32_t)inode_num, &file_node, chunk_offset, sizeof(chunk_hdr), chunk_hdr);
        uint32_t chunk_size;
        memcpy(&chunk_size, chunk_hdr + 4, 4);
        if (memcmp(chunk_hdr, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            uint32_t fmt_len = chunk_size < sizeof(fmt) ? chunk_size : sizeof(fmt);
            fs_read((uint32_t)inode_num, &file_node, chunk_offset + 8, fmt_len, fmt);
            if (fmt_len >= 16) {
                memcpy(&wav_channels, fmt + 2, 2);
                memcpy(&wav_sample_rate, fmt + 4, 4);
            }
        } else if (memcmp(chunk_hdr, "data", 4) == 0) {
            data_offset = chunk_offset + 8;
            total_data = chunk_size;
            break;
        }
        chunk_offset += 8 + chunk_size + (chunk_size & 1);
    }
    if (total_data == 0) {
        kklog("WAV: data chunk not found\n");
        return -1;
    }
    if (wav_channels == 0) wav_channels = 2;
    if (data_offset + total_data > file_node.size) {
        total_data = file_node.size - data_offset;
    }

    if (wav_channels != 2) {
        kklog("WAV: mono files are played at native rate but not up-mixed to stereo\n");
    }

    uint32_t real_bar0 = pci_config_read(g_dev.bus, g_dev.device, g_dev.function, 0x10);
    uint16_t nam_port = (uint16_t)(real_bar0 & ~0x3);
    uint16_t nabm_port = ac97_get_nabm_port();
    if (ac97_reset_stream(nabm_port) != 0) {
        return -1;
    }
    uint32_t applied_rate = ac97_set_rate(nam_port, wav_sample_rate);
    ac97_set_volume(g_ac97_vol_atten);
    outl(nabm_port + 0x10, (uint32_t)&bdl);

    g_ac97_stop_requested = 0;
    g_ac97_paused = 0;

    kklogf_color("Playing: %s (%d bytes @ %d Hz)\n", 0x00FF00, filename, total_data, applied_rate);

    uint32_t offset = data_offset;
    uint32_t remaining = total_data;
    uint32_t filled_total = 0;
    uint32_t consumed_total = 0;
    int started = 0;
    uint8_t prev_civ = 0;

    while (remaining > 0 || consumed_total < filled_total) {
        if (g_ac97_stop_requested) {
            break;
        }
        while (g_ac97_paused && !g_ac97_stop_requested) {
            outb(nabm_port + 0x1B, 0x00);
            asm volatile("pause");
        }
        if (started && !g_ac97_paused && !(inb(nabm_port + 0x1B) & 0x01)) {
            outb(nabm_port + 0x1B, 0x01);
        }

        uint32_t in_flight = filled_total - consumed_total;
        while (remaining > 0 && in_flight < BDL_ENTRIES) {
            uint32_t want = remaining;
            if (want > CHUNK_SIZE) want = CHUNK_SIZE;
            int idx = filled_total % BDL_ENTRIES;
            uint32_t got = fs_read((uint32_t)inode_num, &file_node, offset, want, stream_bufs[idx]);
            if (got == 0) {
                remaining = 0;
                break;
            }
            bdl[idx].buffer_addr = (uint32_t)stream_bufs[idx];
            bdl[idx].length = got / 2;
            bdl[idx].flags = 0x8000;
            offset += got;
            remaining -= got;
            filled_total++;
            in_flight++;
            outb(nabm_port + 0x15, (uint8_t)idx);
        }

        if (!started && filled_total > 0) {
            outb(nabm_port + 0x1B, 0x01);
            prev_civ = inb(nabm_port + 0x14) & 0x1F;
            started = 1;
        }

        if (filled_total == 0) {
            break;
        }

        uint32_t spins = 0;
        uint8_t civ;
        while (1) {
            civ = inb(nabm_port + 0x14) & 0x1F;
            if (civ != prev_civ) break;
            if (inw(nabm_port + 0x16) & 0x1C) break;
            if (++spins > 20000000) break;
            asm volatile("pause");
        }

        uint32_t advanced = (civ - prev_civ) & 0x1F;
        if (advanced > 0) {
            consumed_total += advanced;
            prev_civ = civ;
        } else if (remaining == 0 && (inw(nabm_port + 0x16) & 0x1C)) {
            consumed_total = filled_total;
        }
        outw(nabm_port + 0x16, 0x1C);
    }

    outb(nabm_port + 0x1B, 0x00);
    g_ac97_paused = 0;
    if (g_ac97_stop_requested) {
        g_ac97_stop_requested = 0;
        kklog("WAV: Stopped.\n");
    } else {
        kklog("WAV: Finished.\n");
    }
    return 0;
}