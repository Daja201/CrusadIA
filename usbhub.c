#include "usbhub.h"
#include "usb.h"
#include "klog.h"
#include <string.h>

#define USB_HUB_REQ_GET_STATUS    0x00
#define USB_HUB_REQ_CLEAR_FEATURE 0x01
#define USB_HUB_REQ_SET_FEATURE   0x03
#define USB_HUB_REQ_GET_DESCRIPTOR 0x06

#define USB_PORT_FEAT_CONNECTION   0
#define USB_PORT_FEAT_ENABLE       1
#define USB_PORT_FEAT_RESET        4
#define USB_PORT_FEAT_POWER        8
#define USB_PORT_FEAT_LOWSPEED     9
#define USB_PORT_FEAT_C_CONNECTION 16
#define USB_PORT_FEAT_C_RESET      20

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bNbrPorts;
    uint16_t wHubCharacteristics;
    uint8_t bPwrOn2PwrGood;
    uint8_t bHubContrCurrent;
} __attribute__((packed)) usb_hub_descriptor_t;

typedef struct {
    usb_device_t* dev;
    int num_ports;
    int port_present[USB_MAX_HUB_PORTS];
} usbhub_ctx_t;

static usbhub_ctx_t g_hubs[8];
static int g_hub_count = 0;

static uint32_t usbhub_get_port_status(usb_device_t* dev, int port) {
    uint32_t status = 0;
    usb_control_transfer(dev, 0xA3, USB_HUB_REQ_GET_STATUS, 0, port + 1, &status, 4);
    return status;
}

static void usbhub_set_feature(usb_device_t* dev, int port, int feature) {
    usb_control_transfer(dev, 0x23, USB_HUB_REQ_SET_FEATURE, feature, port + 1, 0, 0);
}

static void usbhub_clear_feature(usb_device_t* dev, int port, int feature) {
    usb_control_transfer(dev, 0x23, USB_HUB_REQ_CLEAR_FEATURE, feature, port + 1, 0, 0);
}

void usbhub_attach(usb_device_t* dev) {
    if (g_hub_count >= 8) return;

    usb_hub_descriptor_t desc;
    if (usb_control_transfer(dev, 0xA0, USB_HUB_REQ_GET_DESCRIPTOR, (USB_DESC_HUB << 8), 0, &desc, sizeof(desc)) < 0) {
        klog("usbhub: failed to read hub descriptor\n");
        return;
    }

    int num_ports = desc.bNbrPorts;
    if (num_ports > USB_MAX_HUB_PORTS) num_ports = USB_MAX_HUB_PORTS;

    for (int i = 0; i < num_ports; i++) {
        usbhub_set_feature(dev, i, USB_PORT_FEAT_POWER);
    }

    usbhub_ctx_t* hub = &g_hubs[g_hub_count++];
    hub->dev = dev;
    hub->num_ports = num_ports;
    memset(hub->port_present, 0, sizeof(hub->port_present));

    klogf_color("usbhub: attached, %d ports\n", 0x00FF00, num_ports);
}

void usbhub_poll(void) {
    for (int h = 0; h < g_hub_count; h++) {
        usbhub_ctx_t* hub = &g_hubs[h];
        for (int p = 0; p < hub->num_ports; p++) {
            uint32_t status = usbhub_get_port_status(hub->dev, p);
            uint32_t change = status >> 16;
            if (change & (1 << USB_PORT_FEAT_C_CONNECTION)) {
                usbhub_clear_feature(hub->dev, p, USB_PORT_FEAT_C_CONNECTION);
                int connected = status & (1 << USB_PORT_FEAT_CONNECTION);
                if (connected && !hub->port_present[p]) {
                    usbhub_set_feature(hub->dev, p, USB_PORT_FEAT_RESET);
                    for (volatile int i = 0; i < 2000000; i++) { asm volatile("nop"); }
                    usbhub_clear_feature(hub->dev, p, USB_PORT_FEAT_C_RESET);
                    uint32_t st2 = usbhub_get_port_status(hub->dev, p);
                    int speed = (st2 & (1 << USB_PORT_FEAT_LOWSPEED)) ? 1 : 0;
                    hub->port_present[p] = 1;
                    usb_enumerate_device(hub->dev->hcd, hub->dev->address, p, speed);
                } else if (!connected) {
                    hub->port_present[p] = 0;
                }
            }
        }
    }
}
