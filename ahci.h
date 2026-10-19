#ifndef AHCI_H
#define AHCI_H
#include <stdint.h>

#define AHCI_MAX_CONTROLLERS 4
#define AHCI_MAX_PORTS 32
#define AHCI_MAX_DEVICES 16
#define AHCI_SECTOR_SIZE 512

typedef enum {
    AHCI_DEV_NONE = 0,
    AHCI_DEV_SATA,
    AHCI_DEV_SATAPI
} ahci_dev_type_t;

typedef struct {
    int present;
    int controller_index;
    int port_index;
    ahci_dev_type_t type;
    uint64_t sector_count;
    uint32_t sector_size;
    char model[41];
    char serial[21];
} ahci_device_t;

void ahci_init(void);
int ahci_device_count(void);
ahci_device_t* ahci_get_device(int index);
int ahci_read_sectors(int dev_index, uint64_t lba, uint32_t count, void* buffer);
int ahci_write_sectors(int dev_index, uint64_t lba, uint32_t count, const void* buffer);
int ahci_identify(int dev_index, uint16_t* out_identify_data);

#endif
