#include "ps2.h"
#include "io.h"
#include "klog.h"
#include "terminal.h"
#include "usbhid.h"

#define PS2_DATA        0x60
#define PS2_STATUS      0x64
#define PS2_COMMAND     0x64

#define PS2_ST_OBF      0x01
#define PS2_ST_IBF      0x02
#define PS2_ST_AUX      0x20
#define PS2_ST_ERR      0xC0

#define PS2_CFG_IRQ1    0x01
#define PS2_CFG_IRQ12   0x02
#define PS2_CFG_CLK1_OFF 0x10
#define PS2_CFG_CLK2_OFF 0x20
#define PS2_CFG_XLATE   0x40

#define PS2_ACK         0xFA
#define PS2_RESEND      0xFE

#define PS2_TABLE_SIZE  84

extern volatile int usb_kbd_dirty;

static int g_controller = 0;
static int g_kbd_ok = 0;
static int g_mouse_ok = 0;

static int g_ext = 0;
static int g_skip = 0;
static int g_lshift = 0;
static int g_rshift = 0;
static int g_caps = 0;

static uint8_t g_mouse_pkt[3];
static int g_mouse_idx = 0;

static const char ps2_lower[PS2_TABLE_SIZE] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '-', '=', '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o', 'p', '[', ']', '\n', 0, 'a', 's', 'd', 'f', 'g', 'h',
    'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '7',
    '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.'
};

static const char ps2_upper[PS2_TABLE_SIZE] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '_', '+', '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', '{', '}', '\n', 0, 'A', 'S', 'D', 'F', 'G', 'H',
    'J', 'K', 'L', ':', '\"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '7',
    '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.'
};

static int ps2_wait_write(void) {
    for (int i = 0; i < 20000; i++) {
        if (!(inb(PS2_STATUS) & PS2_ST_IBF)) return 0;
        pit_delay_us(50);
    }
    return -1;
}

static int ps2_wait_read(int ms) {
    for (int i = 0; i < ms * 20; i++) {
        if (inb(PS2_STATUS) & PS2_ST_OBF) return 0;
        pit_delay_us(50);
    }
    return -1;
}

static int ps2_read(int ms) {
    if (ps2_wait_read(ms) < 0) return -1;
    return inb(PS2_DATA);
}

static int ps2_command(uint8_t cmd) {
    if (ps2_wait_write() < 0) return -1;
    outb(PS2_COMMAND, cmd);
    return 0;
}

static int ps2_data(uint8_t val) {
    if (ps2_wait_write() < 0) return -1;
    outb(PS2_DATA, val);
    return 0;
}

static void ps2_flush(void) {
    for (int i = 0; i < 64; i++) {
        if (!(inb(PS2_STATUS) & PS2_ST_OBF)) break;
        inb(PS2_DATA);
        pit_delay_us(100);
    }
}

static int ps2_read_config(void) {
    if (ps2_command(0x20) < 0) return -1;
    return ps2_read(100);
}

static int ps2_write_config(uint8_t cfg) {
    if (ps2_command(0x60) < 0) return -1;
    return ps2_data(cfg);
}

static int ps2_device_send(int second, uint8_t val) {
    for (int attempt = 0; attempt < 3; attempt++) {
        if (second && ps2_command(0xD4) < 0) return -1;
        if (ps2_data(val) < 0) return -1;
        int r = ps2_read(200);
        if (r == PS2_ACK) return 0;
        if (r != PS2_RESEND) return -1;
    }
    return -1;
}

static void ps2_keyboard_init(void) {
    ps2_command(0xAE);
    ps2_flush();

    if (ps2_device_send(0, 0xFF) < 0) {
        klog_color("ps2: keyboard did not answer reset\n", 0xFFFF00);
        return;
    }
    int bat = ps2_read(1000);
    if (bat != 0xAA) {
        klogf_color("ps2: keyboard self-test returned 0x%x\n", 0xFFFF00, bat < 0 ? 0xFFFF : bat);
    }
    ps2_flush();

    ps2_device_send(0, 0xF5);
    if (ps2_device_send(0, 0xF0) == 0) {
        ps2_device_send(0, 0x02);
    }
    if (ps2_device_send(0, 0xF4) < 0) {
        klog_color("ps2: keyboard refused enable scanning\n", 0xFFFF00);
        return;
    }
    g_kbd_ok = 1;
    klog_status("PS/2 KEYBOARD READY", 0x00FF00);
}

static void ps2_mouse_init(void) {
    ps2_command(0xA8);
    ps2_flush();

    if (ps2_device_send(1, 0xFF) < 0) {
        ps2_command(0xA7);
        return;
    }
    int bat = ps2_read(1000);
    if (bat == 0xAA) {
        ps2_read(200);
    }
    ps2_flush();

    ps2_device_send(1, 0xF6);
    if (ps2_device_send(1, 0xF4) < 0) {
        ps2_command(0xA7);
        return;
    }
    g_mouse_ok = 1;
    klog_status("PS/2 MOUSE READY", 0x00FF00);
}

void ps2_init(void) {
    g_controller = 0;
    g_kbd_ok = 0;
    g_mouse_ok = 0;

    if (inb(PS2_STATUS) == 0xFF) {
        klog_color("ps2: no controller present\n", 0xFFFF00);
        return;
    }

    ps2_command(0xAD);
    ps2_command(0xA7);
    ps2_flush();

    int cfg = ps2_read_config();
    if (cfg < 0) {
        klog_color("ps2: controller did not return config byte\n", 0xFFFF00);
        return;
    }
    cfg &= ~(PS2_CFG_IRQ1 | PS2_CFG_IRQ12 | PS2_CFG_CLK1_OFF | PS2_CFG_CLK2_OFF);
    cfg |= PS2_CFG_XLATE;
    ps2_write_config((uint8_t)cfg);

    if (ps2_command(0xAA) == 0) {
        int st = ps2_read(500);
        if (st != 0x55) {
            klogf_color("ps2: controller self-test returned 0x%x\n", 0xFFFF00, st < 0 ? 0xFFFF : st);
        }
    }
    ps2_flush();
    ps2_write_config((uint8_t)cfg);

    int dual = 0;
    if (ps2_command(0xA8) == 0) {
        int c2 = ps2_read_config();
        if (c2 >= 0 && !(c2 & PS2_CFG_CLK2_OFF)) dual = 1;
        ps2_command(0xA7);
    }
    ps2_flush();

    g_controller = 1;
    ps2_keyboard_init();
    if (dual) ps2_mouse_init();

    int final_cfg = ps2_read_config();
    if (final_cfg >= 0) {
        final_cfg &= ~(PS2_CFG_IRQ1 | PS2_CFG_IRQ12 | PS2_CFG_CLK1_OFF | PS2_CFG_CLK2_OFF);
        final_cfg |= PS2_CFG_XLATE;
        ps2_write_config((uint8_t)final_cfg);
    }
    ps2_flush();

    if (!g_kbd_ok && !g_mouse_ok) {
        klog_color("ps2: no devices found\n", 0xFFFF00);
    }
}

static void ps2_keyboard_byte(uint8_t b) {
    if (g_skip > 0) {
        g_skip--;
        return;
    }
    if (b == 0xE1) {
        g_skip = 5;
        return;
    }
    if (b == 0xE0) {
        g_ext = 1;
        return;
    }
    if (b == 0x00 || b == 0xFF || b == PS2_ACK || b == PS2_RESEND) {
        return;
    }

    int ext = g_ext;
    g_ext = 0;
    int release = (b & 0x80) ? 1 : 0;
    uint8_t code = b & 0x7F;

    if (ext) {
        if (code == 0x2A || code == 0x36) return;
    } else {
        if (code == 0x2A) { g_lshift = !release; return; }
        if (code == 0x36) { g_rshift = !release; return; }
        if (code == 0x3A) {
            if (!release) g_caps ^= 1;
            return;
        }
    }

    if (release) return;

    char c = 0;
    if (ext) {
        switch (code) {
            case 0x48: c = (char)ARROW_UP; break;
            case 0x50: c = (char)ARROW_DOWN; break;
            case 0x4B: c = (char)ARROW_LEFT; break;
            case 0x4D: c = (char)ARROW_RIGHT; break;
            case 0x1C: c = '\n'; break;
            case 0x35: c = '/'; break;
            default: break;
        }
    } else if (code == 0x01) {
        c = (char)ARROW_ESC;
    } else if (code < PS2_TABLE_SIZE) {
        int shift = g_lshift || g_rshift;
        char base = ps2_lower[code];
        if (base >= 'a' && base <= 'z') {
            c = (shift ^ g_caps) ? ps2_upper[code] : base;
        } else {
            c = shift ? ps2_upper[code] : base;
        }
    }

    if (c != 0) {
        terminal_key(c);
        stdin_feed(c);
        usb_kbd_dirty = 1;
    }
}

static void ps2_mouse_byte(uint8_t b) {
    if (g_mouse_idx == 0 && !(b & 0x08)) return;
    g_mouse_pkt[g_mouse_idx++] = b;
    if (g_mouse_idx < 3) return;
    g_mouse_idx = 0;

    uint8_t flags = g_mouse_pkt[0];
    if (flags & 0xC0) return;

    int dx = (int)g_mouse_pkt[1] - (int)((flags << 4) & 0x100);
    int dy = (int)g_mouse_pkt[2] - (int)((flags << 3) & 0x100);
    mouse_apply_motion(dx, -dy, (uint8_t)(flags & 0x07));
}

void ps2_poll(void) {
    if (!g_controller) return;
    for (int guard = 0; guard < 64; guard++) {
        uint8_t st = inb(PS2_STATUS);
        if (!(st & PS2_ST_OBF)) break;
        uint8_t b = inb(PS2_DATA);
        if (st & PS2_ST_ERR) continue;
        if (st & PS2_ST_AUX) {
            if (g_mouse_ok) ps2_mouse_byte(b);
        } else {
            if (g_kbd_ok && !usbhid_keyboard_active()) ps2_keyboard_byte(b);
        }
    }
}

int ps2_keyboard_present(void) { return g_kbd_ok; }
int ps2_mouse_present(void) { return g_mouse_ok; }
