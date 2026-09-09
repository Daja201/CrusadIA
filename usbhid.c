#include "usbhid.h"
#include "usb.h"
#include "terminal.h"
#include "klog.h"

#define HID_SUBCLASS_BOOT 1
#define HID_PROTO_KEYBOARD 1
#define HID_PROTO_MOUSE 2
#define HID_RAW_MAX_DEVICES 4
#define HID_RAW_BUF_LEN 64

typedef struct {
    usb_device_t* dev;
    uint8_t last_report[HID_RAW_BUF_LEN];
    int last_len;
    uint32_t update_count;
    int in_use;
} usbhid_raw_slot_t;

static usbhid_raw_slot_t g_raw_slots[HID_RAW_MAX_DEVICES];

static const char hid_map_lower[104] = {
    0,0,0,0,'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z',
    '1','2','3','4','5','6','7','8','9','0','\n',27,'\b','\t',' ','-','=','[',']','\\',0,';','\'','`',',','.','/',
    0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const char hid_map_upper[104] = {
    0,0,0,0,'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    '!','@','#','$','%','^','&','*','(',')','\n',27,'\b','\t',' ','_','+','{','}','|',0,':','"','~','<','>','?',
    0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

#define HID_MOD_LSHIFT 0x02
#define HID_MOD_RSHIFT 0x20

static uint8_t g_prev_keys[6];
volatile int usb_kbd_dirty = 0;

static void usbhid_keyboard_report(usb_device_t* dev, uint8_t* report, int len) {
    if (len < 8) return;
    uint8_t modifiers = report[0];
    int shift = (modifiers & (HID_MOD_LSHIFT | HID_MOD_RSHIFT)) ? 1 : 0;

    for (int i = 0; i < 6; i++) {
        uint8_t code = report[2 + i];
        if (code == 0) continue;
        int already = 0;
        for (int j = 0; j < 6; j++) {
            if (g_prev_keys[j] == code) { already = 1; break; }
        }
        if (already) continue;
        if (code == 0x4F) { terminal_key((char)2); usb_kbd_dirty = 1; continue; }
        if (code == 0x50) { terminal_key((char)1); usb_kbd_dirty = 1; continue; }
        if (code < 104) {
            char c = shift ? hid_map_upper[code] : hid_map_lower[code];
            if (c != 0) {
                terminal_key(c);
                usb_kbd_dirty = 1;
            }
        }
    }

    for (int i = 0; i < 6; i++) g_prev_keys[i] = report[2 + i];
}

static void usbhid_mouse_report(usb_device_t* dev, uint8_t* report, int len) {
    if (len < 3) return;
    int8_t dx = (int8_t)report[1];
    int8_t dy = (int8_t)report[2];
    (void)dx;
    (void)dy;
}

static void usbhid_raw_report(usb_device_t* dev, uint8_t* report, int len) {
    for (int i = 0; i < HID_RAW_MAX_DEVICES; i++) {
        if (g_raw_slots[i].in_use && g_raw_slots[i].dev == dev) {
            int copy_len = len > HID_RAW_BUF_LEN ? HID_RAW_BUF_LEN : len;
            for (int j = 0; j < copy_len; j++) {
                g_raw_slots[i].last_report[j] = report[j];
            }
            g_raw_slots[i].last_len = copy_len;
            g_raw_slots[i].update_count++;
            return;
        }
    }
}

int usbhid_raw_slot_count(void) {
    int c = 0;
    for (int i = 0; i < HID_RAW_MAX_DEVICES; i++) {
        if (g_raw_slots[i].in_use) c++;
    }
    return c;
}

usb_device_t* usbhid_raw_slot_device(int index) {
    int count = 0;
    for (int i = 0; i < HID_RAW_MAX_DEVICES; i++) {
        if (g_raw_slots[i].in_use) {
            if (count == index) return g_raw_slots[i].dev;
            count++;
        }
    }
    return 0;
}

int usbhid_raw_slot_report(int index, uint8_t* out_buf, int max_len, uint32_t* out_update_count) {
    int count = 0;
    for (int i = 0; i < HID_RAW_MAX_DEVICES; i++) {
        if (g_raw_slots[i].in_use) {
            if (count == index) {
                int copy_len = g_raw_slots[i].last_len > max_len ? max_len : g_raw_slots[i].last_len;
                for (int j = 0; j < copy_len; j++) {
                    out_buf[j] = g_raw_slots[i].last_report[j];
                }
                if (out_update_count) *out_update_count = g_raw_slots[i].update_count;
                return copy_len;
            }
            count++;
        }
    }
    return -1;
}

void usbhid_attach(usb_device_t* dev) {
    if (dev->ep_in_addr == 0) {
        klog("usbhid: no interrupt-in endpoint found\n");
        return;
    }

    if (dev->iface_subclass == HID_SUBCLASS_BOOT) {
        usb_control_transfer(dev, 0x21, USB_REQ_SET_PROTOCOL, 0, 0, 0, 0);
        usb_control_transfer(dev, 0x21, USB_REQ_SET_IDLE, 0, 0, 0, 0);

        if (dev->iface_protocol == HID_PROTO_KEYBOARD) {
            for (int i = 0; i < 6; i++) g_prev_keys[i] = 0;
            dev->hcd->setup_interrupt_in(dev->hcd, dev, dev->ep_in_addr, dev->ep_in_maxpkt, dev->ep_in_interval, usbhid_keyboard_report);
            klog_status("USB KEYBOARD READY", 0x00FF00);
            return;
        } else if (dev->iface_protocol == HID_PROTO_MOUSE) {
            dev->hcd->setup_interrupt_in(dev->hcd, dev, dev->ep_in_addr, dev->ep_in_maxpkt, dev->ep_in_interval, usbhid_mouse_report);
            klog_status("USB MOUSE READY", 0x00FF00);
            return;
        }
    }

    usb_control_transfer(dev, 0x21, USB_REQ_SET_IDLE, 0, 0, 0, 0);

    for (int i = 0; i < HID_RAW_MAX_DEVICES; i++) {
        if (!g_raw_slots[i].in_use) {
            g_raw_slots[i].in_use = 1;
            g_raw_slots[i].dev = dev;
            g_raw_slots[i].last_len = 0;
            g_raw_slots[i].update_count = 0;
            dev->hcd->setup_interrupt_in(dev->hcd, dev, dev->ep_in_addr, dev->ep_in_maxpkt, dev->ep_in_interval, usbhid_raw_report);
            klog_status("USB HID DEVICE READY (raw mode)", 0x00FF00);
            return;
        }
    }

    klog("usbhid: raw HID slot table full\n");
}