#ifndef PCI_H
#define PCI_H
#include <stdint.h>

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint32_t bar0;
    uint32_t bar1;
} pci_device_t;

uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void pci_config_write(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
int pci_find_class(uint8_t class_code, uint8_t subclass, pci_device_t* out, int index);

#endif
