#include "usb.h"
#include "usbhid.h"
#include "usbhub.h"
#include "klog.h"
#include <string.h>

static usb_device_t g_devices[USB_MAX_DEVICES];
static int g_next_address = 1;

void usb_init(void) {
    memset(g_devices, 0, sizeof(g_devices));
    g_next_address = 1;
}

usb_device_t* usb_alloc_device(usb_hcd_t* hcd) {
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (!g_devices[i].in_use) {
            memset(&g_devices[i], 0, sizeof(usb_device_t));
            g_devices[i].in_use = 1;
            g_devices[i].hcd = hcd;
            g_devices[i].max_packet0 = 8;
            return &g_devices[i];
        }
    }
    return 0;
}

void usb_free_device(usb_device_t* dev) {
    if (dev) dev->in_use = 0;
}

int usb_control_transfer(usb_device_t* dev, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, void* buf, uint16_t wLength) {
    usb_setup_pkt_t setup;
    setup.bmRequestType = bmRequestType;
    setup.bRequest = bRequest;
    setup.wValue = wValue;
    setup.wIndex = wIndex;
    setup.wLength = wLength;
    int dir_in = (bmRequestType & 0x80) ? 1 : 0;
    return dev->hcd->control_transfer(dev->hcd, dev, &setup, buf, wLength, dir_in);
}

int usb_get_descriptor(usb_device_t* dev, uint8_t type, uint8_t index, void* buf, uint16_t len) {
    return usb_control_transfer(dev, 0x80, USB_REQ_GET_DESCRIPTOR, (type << 8) | index, 0, buf, len);
}

int usb_set_address(usb_device_t* dev, int addr) {
    int r = usb_control_transfer(dev, 0x00, USB_REQ_SET_ADDRESS, addr, 0, 0, 0);
    if (r >= 0) dev->address = addr;
    return r;
}

int usb_set_configuration(usb_device_t* dev, int config) {
    return usb_control_transfer(dev, 0x00, USB_REQ_SET_CONFIGURATION, config, 0, 0, 0);
}

static void usb_parse_config(usb_device_t* dev, uint8_t* cfgbuf, int total_len) {
    uint8_t* p = cfgbuf;
    uint8_t* end = cfgbuf + total_len;
    int found_iface = 0;
    while (p < end) {
        uint8_t len = p[0];
        uint8_t type = p[1];
        if (len == 0) break;
        if (type == USB_DESC_INTERFACE && !found_iface) {
            usb_interface_descriptor_t* id = (usb_interface_descriptor_t*)p;
            dev->iface_class = id->bInterfaceClass;
            dev->iface_subclass = id->bInterfaceSubClass;
            dev->iface_protocol = id->bInterfaceProtocol;
            found_iface = 1;
        } else if (type == USB_DESC_ENDPOINT && found_iface && dev->ep_in_addr == 0) {
            usb_endpoint_descriptor_t* ed = (usb_endpoint_descriptor_t*)p;
            if ((ed->bmAttributes & 0x03) == 0x03 && (ed->bEndpointAddress & 0x80)) {
                dev->ep_in_addr = ed->bEndpointAddress;
                dev->ep_in_maxpkt = ed->wMaxPacketSize & 0x7FF;
                dev->ep_in_interval = ed->bInterval ? ed->bInterval : 10;
            }
        }
        p += len;
    }
}

void usb_enumerate_device(usb_hcd_t* hcd, int hub_addr, int hub_port, int speed) {
    usb_device_t* dev = usb_alloc_device(hcd);
    if (!dev) return;
    dev->address = 0;
    dev->hub_addr = hub_addr;
    dev->hub_port = hub_port;
    dev->speed = speed;
    dev->max_packet0 = 8;

    uint8_t tmp[8];
    if (usb_get_descriptor(dev, USB_DESC_DEVICE, 0, tmp, 8) < 0) {
        klog("usb: failed to read initial device descriptor\n");
        usb_free_device(dev);
        return;
    }
    usb_device_descriptor_t* dd8 = (usb_device_descriptor_t*)tmp;
    dev->max_packet0 = dd8->bMaxPacketSize0 ? dd8->bMaxPacketSize0 : 8;

    int addr = g_next_address++;
    if (usb_set_address(dev, addr) < 0) {
        klog("usb: set address failed\n");
        usb_free_device(dev);
        return;
    }

    if (usb_get_descriptor(dev, USB_DESC_DEVICE, 0, &dev->dev_desc, sizeof(usb_device_descriptor_t)) < 0) {
        klog("usb: failed to read full device descriptor\n");
        usb_free_device(dev);
        return;
    }

    usb_config_descriptor_t cfg9;
    if (usb_get_descriptor(dev, USB_DESC_CONFIGURATION, 0, &cfg9, sizeof(cfg9)) < 0) {
        klog("usb: failed to read config header\n");
        usb_free_device(dev);
        return;
    }

    int total = cfg9.wTotalLength;
    if (total > 512) total = 512;
    uint8_t cfgbuf[512];
    if (usb_get_descriptor(dev, USB_DESC_CONFIGURATION, 0, cfgbuf, total) < 0) {
        klog("usb: failed to read full config descriptor\n");
        usb_free_device(dev);
        return;
    }

    usb_parse_config(dev, cfgbuf, total);

    if (usb_set_configuration(dev, cfg9.bConfigurationValue) < 0) {
        klog("usb: set configuration failed\n");
        usb_free_device(dev);
        return;
    }

    klogf_color("usb: dev addr=%d vid=0x%x pid=0x%x class=0x%x sub=0x%x\n", 0x00FF00,
        dev->address, dev->dev_desc.idVendor, dev->dev_desc.idProduct, dev->iface_class, dev->iface_subclass);

    if (dev->iface_class == USB_CLASS_HID) {
        usbhid_attach(dev);
    } else if (dev->iface_class == USB_CLASS_HUB) {
        usbhub_attach(dev);
    }
}

void usb_poll(void) {
    usbhub_poll();
}

int usb_device_count(void) {
    int count = 0;
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (g_devices[i].in_use) count++;
    }
    return count;
}

usb_device_t* usb_get_device(int index) {
    int count = 0;
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (g_devices[i].in_use) {
            if (count == index) return &g_devices[i];
            count++;
        }
    }
    return 0;
}

const char* usb_speed_str(int speed) {
    if (speed == 2) return "High (480 Mbit)";
    if (speed == 1) return "Low (1.5 Mbit)";
    return "Full (12 Mbit)";
}

const char* usb_class_str(uint8_t class_code) {
    switch (class_code) {
        case 0x03: return "HID";
        case 0x08: return "Mass Storage";
        case 0x09: return "Hub";
        case 0x01: return "Audio";
        case 0x02: return "CDC/Communications";
        case 0x0A: return "CDC Data";
        case 0x0B: return "Smart Card";
        case 0x0E: return "Video";
        case 0xE0: return "Wireless";
        case 0xEF: return "Misc";
        case 0xFE: return "App Specific";
        case 0xFF: return "Vendor Specific";
        default: return "Unknown";
    }
}