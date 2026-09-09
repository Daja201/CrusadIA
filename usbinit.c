#include "pci.h"
#include "usb.h"
#include "ohci.h"
#include "ehci.h"
#include "klog.h"

#define PCI_CLASS_SERIAL_BUS 0x0C
#define PCI_SUBCLASS_USB     0x03
#define PCI_PROGIF_OHCI      0x10
#define PCI_PROGIF_EHCI      0x20

void ehci_bios_handoff_pci(uint8_t bus, uint8_t dev, uint8_t func, uint32_t bar0_phys);

static pci_device_t g_ohci_devices[8];
static int g_ohci_count = 0;

static void usb_release_port_noop(int port) {
    (void)port;
}

void usb_pci_init(void) {
    usb_init();

    int index = 0;
    pci_device_t dev;

    while (pci_find_class(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB, &dev, index) == 0) {
        uint32_t progif = (pci_config_read(dev.bus, dev.device, dev.function, 0x08) >> 8) & 0xFF;
        if (progif == PCI_PROGIF_EHCI) {
            uint32_t cmd = pci_config_read(dev.bus, dev.device, dev.function, 0x04);
            cmd |= 0x06;
            pci_config_write(dev.bus, dev.device, dev.function, 0x04, cmd);
            uint32_t bar0 = dev.bar0 & ~0xF;
            ehci_bios_handoff_pci(dev.bus, dev.device, dev.function, bar0);
            klogf_color("usb: found EHCI controller at %d:%d.%d bar0=0x%x\n", 0x00FF00, dev.bus, dev.device, dev.function, bar0);
            ehci_probe_and_init(bar0, usb_release_port_noop);
        } else if (progif == PCI_PROGIF_OHCI) {
            if (g_ohci_count < 8) {
                g_ohci_devices[g_ohci_count++] = dev;
            }
        }
        index++;
    }

    for (int i = 0; i < g_ohci_count; i++) {
        pci_device_t* d = &g_ohci_devices[i];
        uint32_t cmd = pci_config_read(d->bus, d->device, d->function, 0x04);
        cmd |= 0x06;
        pci_config_write(d->bus, d->device, d->function, 0x04, cmd);
        uint32_t bar0 = d->bar0 & ~0xF;
        klogf_color("usb: found OHCI controller at %d:%d.%d bar0=0x%x\n", 0x00FF00, d->bus, d->device, d->function, bar0);
        ohci_probe_and_init(bar0);
    }
}

void usb_poll_all(void) {
    ohci_poll_interrupts();
    ehci_poll_interrupts();
    usb_poll();
}