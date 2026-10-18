#include "pci.h"
#include "klog.h"
#include "bootinfo.h"

#define PCI_MMIO32_FLOOR    0x80000000u
#define PCI_MMIO32_CEILING  0xFEC00000u

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

static uint32_t pci_bar32_size(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t orig = pci_config_read(bus, device, function, offset);
    pci_config_write(bus, device, function, offset, 0xFFFFFFFFu);
    uint32_t probe = pci_config_read(bus, device, function, offset);
    pci_config_write(bus, device, function, offset, orig);
    uint32_t mask = probe & ~0xFu;
    if (mask == 0) return 0;
    return (~mask) + 1u;
}

static uint32_t pci_mmio32_ceiling(void) {
    uint32_t ceiling = 0;
    for (uint16_t bus = 0; bus < 16; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t v = pci_config_read((uint8_t)bus, dev, func, 0x00);
                if ((v & 0xFFFF) == 0xFFFF) continue;
                for (uint8_t bar = 0x10; bar <= 0x24; bar += 4) {
                    uint32_t val = pci_config_read((uint8_t)bus, dev, func, bar);
                    if (val == 0 || (val & 0x1)) continue;
                    uint32_t type = (val >> 1) & 0x3;
                    uint32_t base = val & ~0xFu;
                    if (base > ceiling) ceiling = base;
                    if (type == 2) bar += 4;
                }
            }
        }
    }
    return ceiling;
}

int pci_relocate_bar64_below_4g(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_offset, uint32_t* out_addr32) {
    uint32_t lo = pci_config_read(bus, device, function, bar_offset);
    if ((lo & 0x1) || (((lo >> 1) & 0x3) != 2)) return -1;

    uint32_t size = pci_bar32_size(bus, device, function, bar_offset);
    if (size == 0) return -1;

    uint32_t align = size;
    if (align < 0x100000u) align = 0x100000u;

    uint32_t floor = pci_mmio32_ceiling();
    if (boot_ram_top_below_4g > floor) floor = boot_ram_top_below_4g;
    if (floor < PCI_MMIO32_FLOOR) floor = PCI_MMIO32_FLOOR;

    uint32_t wanted = floor + align * 4u;
    uint32_t base = (wanted + (align - 1u)) & ~(align - 1u);
    if (base < wanted) return -1;
    if ((uint64_t)base + (uint64_t)size > (uint64_t)PCI_MMIO32_CEILING) return -1;

    uint32_t flags_lo = lo & 0xFu;
    uint32_t cmd = pci_config_read(bus, device, function, 0x04);
    pci_config_write(bus, device, function, 0x04, cmd & ~0x2u);

    pci_config_write(bus, device, function, bar_offset, base | flags_lo);
    pci_config_write(bus, device, function, bar_offset + 4, 0);

    pci_config_write(bus, device, function, 0x04, cmd | 0x6u);

    uint32_t check_lo = pci_config_read(bus, device, function, bar_offset) & ~0xFu;
    uint32_t check_hi = pci_config_read(bus, device, function, bar_offset + 4);
    if (check_lo != base || check_hi != 0) return -1;

    *out_addr32 = base;
    return 0;
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
                        uint32_t bar0_lo = pci_config_read(bus, dev, func, 0x10);
                        uint64_t bar_phys = bar0_lo & ~0xFu;
                        if (((bar0_lo >> 1) & 0x3) == 2) {
                            uint32_t bar1 = pci_config_read(bus, dev, func, 0x14);
                            bar_phys |= ((uint64_t)bar1) << 32;
                        }
                        out->bar0 = bar_phys;
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