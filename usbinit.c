#include "pci.h"
#include "usb.h"
#include "ohci.h"
#include "ehci.h"
#include "xhci.h"
#include "uhci.h"
#include "klog.h"

void ehci_bios_handoff_pci(uint8_t bus, uint8_t dev, uint8_t func, uint32_t bar0_phys);

static pci_device_t g_ohci_devices[8];
static int g_ohci_count = 0;
static pci_device_t g_uhci_devices[8];
static int g_uhci_count = 0;

static void usb_release_port_noop(int port) {
    (void)port;
}

static int usb_resolve_bar0(pci_device_t* dev, const char* what, uint32_t* out_addr32) {
    if (dev->bar0 <= 0xFFFFFFFFull) {
        *out_addr32 = (uint32_t)dev->bar0;
        return 1;
    }

    uint32_t relocated = 0;
    if (pci_relocate_bar64_below_4g(dev->bus, dev->device, dev->function, 0x10, &relocated) == 0) {
        *out_addr32 = relocated;
        return 1;
    }

    (void)what;
    klog_color("sysmsg: usb device error data controller bar above 4GB\n", 0xFFFF00);
    return 0;
}

void usb_pci_init(void) {
    usb_init();

    int index = 0;
    pci_device_t dev;

    while (pci_find_class(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB, &dev, index) == 0) {
        uint32_t progif = (pci_config_read(dev.bus, dev.device, dev.function, 0x08) >> 8) & 0xFF;
        if (progif == PCI_PROGIF_EHCI) {
            uint32_t bar0;
            if (usb_resolve_bar0(&dev, "EHCI controller", &bar0)) {
                uint32_t cmd = pci_config_read(dev.bus, dev.device, dev.function, 0x04);
                cmd |= 0x06;
                pci_config_write(dev.bus, dev.device, dev.function, 0x04, cmd);
                bar0 &= ~0xFu;
                ehci_bios_handoff_pci(dev.bus, dev.device, dev.function, bar0);
                ehci_probe_and_init(bar0, usb_release_port_noop);
            }
        } else if (progif == PCI_PROGIF_OHCI) {
            if (g_ohci_count < 8) {
                g_ohci_devices[g_ohci_count++] = dev;
            }
        } else if (progif == PCI_PROGIF_XHCI) {
            uint32_t bar0;
            if (usb_resolve_bar0(&dev, "xHCI controller", &bar0)) {
                uint32_t cmd = pci_config_read(dev.bus, dev.device, dev.function, 0x04);
                cmd |= 0x06;
                pci_config_write(dev.bus, dev.device, dev.function, 0x04, cmd);
                bar0 &= ~0xFu;
                xhci_probe_and_init(dev.bus, dev.device, dev.function, bar0);
            }
        } else if (progif == PCI_PROGIF_UHCI) {
            if (g_uhci_count < 8) {
                g_uhci_devices[g_uhci_count++] = dev;
            }
        }
        index++;
    }

    for (int i = 0; i < g_ohci_count; i++) {
        pci_device_t* d = &g_ohci_devices[i];
        uint32_t bar0;
        if (!usb_resolve_bar0(d, "OHCI controller", &bar0)) continue;
        uint32_t cmd = pci_config_read(d->bus, d->device, d->function, 0x04);
        cmd |= 0x06;
        pci_config_write(d->bus, d->device, d->function, 0x04, cmd);
        bar0 &= ~0xFu;
        ohci_probe_and_init(bar0);
    }

    for (int i = 0; i < g_uhci_count; i++) {
        pci_device_t* d = &g_uhci_devices[i];
        uint32_t bar4 = pci_config_read(d->bus, d->device, d->function, 0x20);
        if (!(bar4 & 0x1)) {
            klog_color("sysmsg: usb device error data uhci controller has no io bar\n", 0xFFFF00);
            continue;
        }
        uint16_t io = (uint16_t)(bar4 & 0xFFFCu);
        uint32_t cmd = pci_config_read(d->bus, d->device, d->function, 0x04);
        cmd |= 0x05;
        pci_config_write(d->bus, d->device, d->function, 0x04, cmd);
        uhci_probe_and_init(d->bus, d->device, d->function, io);
    }
}

void usb_poll_all(void) {
    static volatile int in_poll = 0;
    if (in_poll) return;
    in_poll = 1;
    ohci_poll_interrupts();
    ehci_poll_interrupts();
    xhci_poll_interrupts();
    uhci_poll_interrupts();
    usb_poll();
    in_poll = 0;
}