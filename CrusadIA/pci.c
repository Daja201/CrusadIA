#include "pci.h"
#include "klog.h"

static inline void outl(unsigned short port, unsigned int val) {
    asm volatile ("outl %0, %1" : : "a" (val), "Nd" (port));
}

static inline unsigned int inl(unsigned short port) {
    unsigned int ret;
    asm volatile ("inl %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}

uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = (uint32_t)(0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)device << 11) | ((uint32_t)function << 8) | (offset & 0xFC));
    outl(0xCF8, address);
    return inl(0xCFC);
}

void pci_config_write(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)(0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)device << 11) | ((uint32_t)function << 8) | (offset & 0xFC));
    outl(0xCF8, address);
    outl(0xCFC, value);
}

int pci_find_class(uint8_t class_code, uint8_t subclass, pci_device_t* out, int index) {
    int found = 0;
    for (uint8_t bus = 0; bus < 16; bus++) {
        //kklogf("pci: scanning bus %d\n", bus);
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t v = pci_config_read(bus, dev, func, 0x00);
                uint16_t vendor = v & 0xFFFF;
                if (vendor == 0xFFFF) continue;
                uint32_t cls = pci_config_read(bus, dev, func, 0x08);
                uint8_t cls_code = (cls >> 24) & 0xFF;
                uint8_t sc = (cls >> 16) & 0xFF;
                //kklogf("pci: device found at %d:%d.%d vendor=0x%x class=0x%x/%x\n", bus, dev, func, vendor, cls_code, sc);
                if (cls_code == class_code && sc == subclass) {
                    if (found == index) {
                        out->bus = bus;
                        out->device = dev;
                        out->function = func;
                        out->vendor_id = vendor;
                        out->device_id = (v >> 16) & 0xFFFF;
                        out->class_code = cls_code;
                        out->subclass = sc;
                        uint32_t bar0 = pci_config_read(bus, dev, func, 0x10);
                        out->bar0 = bar0;
                        return 0;
                    }
                    found++;
                }
                if (func == 0) {
                    uint32_t hdr = pci_config_read(bus, dev, func, 0x0C);
                }
            }
        }
    }
    return -1;
}
