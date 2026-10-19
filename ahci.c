#include "ahci.h"
#include "pci.h"
#include "pmm.h"
#include "paging.h"
#include "klog.h"
#include "string.h"

#define AHCI_MMIO_MAP_SIZE 0x1100
#define AHCI_MMIO_UC_FLAGS 0x1B

#define HBA_GHC_AE   (1u << 31)
#define HBA_GHC_IE   (1u << 1)
#define HBA_GHC_HR   (1u << 0)

#define HBA_PORT_CMD_ST  (1u << 0)
#define HBA_PORT_CMD_FRE (1u << 4)
#define HBA_PORT_CMD_FR  (1u << 14)
#define HBA_PORT_CMD_CR  (1u << 15)
#define HBA_PORT_CMD_POD (1u << 2)
#define HBA_PORT_CMD_SUD (1u << 1)

#define HBA_PORT_IS_TFES (1u << 30)

#define HBA_PxSSTS_DET_MASK 0xF
#define HBA_PxSSTS_IPM_MASK 0xF00

#define AHCI_SIG_ATA   0x00000101
#define AHCI_SIG_ATAPI 0xEB140101
#define AHCI_SIG_SEMB  0xC33C0101
#define AHCI_SIG_PM    0x96690101

#define AHCI_CMD_SLOTS 32
#define AHCI_CMD_FIS_LEN 20
#define AHCI_PRDT_ENTRIES 8
#define AHCI_SPIN_LIMIT 10000000u

#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_IDENTIFY_PACKET 0xA1
#define ATA_CMD_READ_DMA_EXT 0x25
#define ATA_CMD_WRITE_DMA_EXT 0x35

#define FIS_TYPE_REG_H2D 0x27
#define FIS_TYPE_REG_D2H 0x34

typedef volatile struct {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t reserved0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t reserved1[11];
    uint32_t vendor[4];
} __attribute__((packed)) hba_port_t;

typedef volatile struct {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t reserved[0xA0 - 0x2C];
    uint8_t vendor[0x100 - 0xA0];
    hba_port_t ports[32];
} __attribute__((packed)) hba_mem_t;

typedef struct {
    uint8_t  fis_type;
    uint8_t  pm_port_c;
    uint8_t  command;
    uint8_t  featurel;
    uint8_t  lba0;
    uint8_t  lba1;
    uint8_t  lba2;
    uint8_t  device;
    uint8_t  lba3;
    uint8_t  lba4;
    uint8_t  lba5;
    uint8_t  featureh;
    uint8_t  countl;
    uint8_t  counth;
    uint8_t  icc;
    uint8_t  control;
    uint8_t  rsv1[4];
} __attribute__((packed)) fis_reg_h2d_t;

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved0;
    uint32_t dbc_flags;
} __attribute__((packed)) hba_prdt_entry_t;

typedef struct {
    uint8_t  cfis[64];
    uint8_t  acmd[16];
    uint8_t  reserved[48];
    hba_prdt_entry_t prdt[AHCI_PRDT_ENTRIES];
} __attribute__((packed)) hba_cmd_tbl_t;

typedef struct {
    uint16_t flags;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved[4];
} __attribute__((packed)) hba_cmd_header_t;

typedef struct {
    hba_cmd_header_t* headers;
    uint8_t* fis;
    hba_cmd_tbl_t* tables[AHCI_CMD_SLOTS];
} ahci_port_region_t;

typedef struct {
    int valid;
    hba_mem_t* abar;
    int n_ports_total;
    int n_cmd_slots;
    int port_to_dev[32];
    ahci_port_region_t port_region[32];
} ahci_controller_t;

static ahci_controller_t g_controllers[AHCI_MAX_CONTROLLERS];
static int g_controller_count = 0;
static ahci_device_t g_devices[AHCI_MAX_DEVICES];
static int g_device_count = 0;

static int ahci_resolve_bar5(pci_device_t* dev, uint32_t* out_addr32) {
    uint32_t bar5 = pci_config_read(dev->bus, dev->device, dev->function, 0x24);
    uint32_t type = (bar5 >> 1) & 0x3;
    if (type == 2) {
        uint32_t bar5_hi = pci_config_read(dev->bus, dev->device, dev->function, 0x28);
        uint64_t addr64 = ((uint64_t)bar5_hi << 32) | (uint64_t)(bar5 & ~0xFu);
        if (addr64 <= 0xFFFFFFFFull) {
            *out_addr32 = (uint32_t)addr64;
            return 1;
        }
        uint32_t relocated = 0;
        if (pci_relocate_bar64_below_4g(dev->bus, dev->device, dev->function, 0x24, &relocated) == 0) {
            *out_addr32 = relocated;
            return 1;
        }
        return 0;
    }
    *out_addr32 = bar5 & ~0xFu;
    return 1;
}

static void ahci_map_mmio(uint32_t base) {
    for (uint32_t off = 0; off < AHCI_MMIO_MAP_SIZE; off += 4096) {
        map_page(base + off, base + off, AHCI_MMIO_UC_FLAGS);
    }
}

static void ahci_spin_clear(volatile uint32_t* reg, uint32_t mask) {
    uint32_t spins = AHCI_SPIN_LIMIT;
    while ((*reg & mask) && spins--) { }
}

static void ahci_stop_port(hba_port_t* port) {
    port->cmd &= ~HBA_PORT_CMD_ST;
    ahci_spin_clear(&port->cmd, HBA_PORT_CMD_CR);
    port->cmd &= ~HBA_PORT_CMD_FRE;
    ahci_spin_clear(&port->cmd, HBA_PORT_CMD_FR);
}

static void ahci_start_port(hba_port_t* port) {
    ahci_spin_clear(&port->cmd, HBA_PORT_CMD_CR);
    port->cmd |= HBA_PORT_CMD_POD;
    port->cmd |= HBA_PORT_CMD_SUD;
    port->cmd |= HBA_PORT_CMD_FRE;
    port->cmd |= HBA_PORT_CMD_ST;
}

static int ahci_find_free_slot(ahci_controller_t* ctrl, hba_port_t* port) {
    uint32_t active = port->sact | port->ci;
    for (int i = 0; i < ctrl->n_cmd_slots; i++) {
        if (!(active & (1u << i))) return i;
    }
    return -1;
}

static int ahci_port_present(hba_port_t* port) {
    uint32_t ssts = port->ssts;
    uint8_t det = ssts & HBA_PxSSTS_DET_MASK;
    uint8_t ipm = (ssts >> 8) & 0xF;
    if (det != 3) return 0;
    if (ipm != 1) return 0;
    return 1;
}

static ahci_dev_type_t ahci_classify_sig(uint32_t sig) {
    if (sig == AHCI_SIG_ATAPI) return AHCI_DEV_SATAPI;
    if (sig == AHCI_SIG_ATA) return AHCI_DEV_SATA;
    return AHCI_DEV_NONE;
}

static int ahci_init_port_region(ahci_controller_t* ctrl, int port_idx) {
    hba_port_t* port = &ctrl->abar->ports[port_idx];
    ahci_stop_port(port);

    ahci_port_region_t* region = &ctrl->port_region[port_idx];
    memset(region, 0, sizeof(ahci_port_region_t));

    uint8_t* clist_block = (uint8_t*)pmm_alloc_block();
    if (!clist_block) return -1;
    memset(clist_block, 0, 4096);

    region->headers = (hba_cmd_header_t*)clist_block;
    region->fis = clist_block + 1024;

    int slots_ready = 0;
    for (int i = 0; i < ctrl->n_cmd_slots; i++) {
        void* tbl = pmm_alloc_block();
        if (!tbl) break;
        memset(tbl, 0, sizeof(hba_cmd_tbl_t));
        region->tables[i] = (hba_cmd_tbl_t*)tbl;
        region->headers[i].ctba = (uint32_t)(uintptr_t)tbl;
        region->headers[i].ctbau = 0;
        region->headers[i].prdtl = 1;
        slots_ready++;
    }

    if (slots_ready == 0) {
        pmm_free_block(clist_block);
        region->headers = 0;
        region->fis = 0;
        return -1;
    }
    ctrl->n_cmd_slots = slots_ready < ctrl->n_cmd_slots ? slots_ready : ctrl->n_cmd_slots;

    port->clb = (uint32_t)(uintptr_t)region->headers;
    port->clbu = 0;
    port->fb = (uint32_t)(uintptr_t)region->fis;
    port->fbu = 0;

    port->serr = 0xFFFFFFFFu;
    port->is = 0xFFFFFFFFu;
    port->ie = 0;

    ahci_start_port(port);
    return 0;
}

static int ahci_run_command(ahci_controller_t* ctrl, int port_idx, uint8_t ata_cmd,
                             uint64_t lba, uint32_t sector_count, void* buffer, int is_write) {
    hba_port_t* port = &ctrl->abar->ports[port_idx];
    ahci_port_region_t* region = &ctrl->port_region[port_idx];
    if (!region->headers || !region->tables[0]) return -1;

    int slot = ahci_find_free_slot(ctrl, port);
    if (slot < 0) return -1;
    if (!region->tables[slot]) return -1;

    uint32_t buf_phys = (uint32_t)(uintptr_t)buffer;
    uint32_t byte_count = sector_count * AHCI_SECTOR_SIZE;

    hba_cmd_header_t* header = &region->headers[slot];
    hba_cmd_tbl_t* table = region->tables[slot];
    memset(table, 0, sizeof(hba_cmd_tbl_t));

    table->prdt[0].dba = buf_phys;
    table->prdt[0].dbau = 0;
    table->prdt[0].dbc_flags = (byte_count - 1) | (1u << 31);

    header->prdtl = 1;
    header->prdbc = 0;
    header->flags = (uint16_t)(5 | (is_write ? (1u << 6) : 0));

    fis_reg_h2d_t* fis = (fis_reg_h2d_t*)table->cfis;
    memset(fis, 0, sizeof(fis_reg_h2d_t));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pm_port_c = 0x80;
    fis->command = ata_cmd;
    fis->device = 1u << 6;

    fis->lba0 = (uint8_t)(lba & 0xFF);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xFF);

    fis->countl = (uint8_t)(sector_count & 0xFF);
    fis->counth = (uint8_t)((sector_count >> 8) & 0xFF);

    uint32_t spins = AHCI_SPIN_LIMIT;
    while ((port->tfd & 0x88) && spins--) { }
    if (spins == 0) return -1;

    port->ci |= (1u << slot);

    spins = AHCI_SPIN_LIMIT;
    int completed = 0;
    while (spins--) {
        if (!(port->ci & (1u << slot))) {
            completed = 1;
            break;
        }
        if (port->is & HBA_PORT_IS_TFES) return -1;
    }
    if (!completed) return -1;
    if (port->is & HBA_PORT_IS_TFES) return -1;

    return 0;
}

static void ahci_swap_ata_string(const uint16_t* words, int word_count, char* out, int out_len) {
    int o = 0;
    for (int i = 0; i < word_count && o + 1 < out_len; i++) {
        char hi = (char)(words[i] >> 8);
        char lo = (char)(words[i] & 0xFF);
        if (o + 1 < out_len) out[o++] = hi;
        if (o + 1 < out_len) out[o++] = lo;
    }
    out[o] = '\0';
    for (int i = o - 1; i >= 0 && out[i] == ' '; i--) out[i] = '\0';
}

static int ahci_identify_device(ahci_controller_t* ctrl, int port_idx, ahci_device_t* dev) {
    uint16_t* buf = (uint16_t*)pmm_alloc_block();
    if (!buf) return -1;
    memset(buf, 0, 4096);

    uint8_t cmd = (dev->type == AHCI_DEV_SATAPI) ? ATA_CMD_IDENTIFY_PACKET : ATA_CMD_IDENTIFY;

    if (ahci_run_command(ctrl, port_idx, cmd, 0, 1, buf, 0) != 0) {
        pmm_free_block(buf);
        return -1;
    }

    if (dev->type == AHCI_DEV_SATA) {
        uint64_t sectors48 = ((uint64_t)buf[100]) | ((uint64_t)buf[101] << 16) |
                              ((uint64_t)buf[102] << 32) | ((uint64_t)buf[103] << 48);
        uint32_t sectors28 = ((uint32_t)buf[61] << 16) | buf[60];
        dev->sector_count = sectors48 != 0 ? sectors48 : sectors28;
        dev->sector_size = AHCI_SECTOR_SIZE;
    } else {
        dev->sector_count = 0;
        dev->sector_size = AHCI_SECTOR_SIZE;
    }

    ahci_swap_ata_string(&buf[27], 20, dev->model, sizeof(dev->model));
    ahci_swap_ata_string(&buf[10], 10, dev->serial, sizeof(dev->serial));

    pmm_free_block(buf);
    return 0;
}

static void ahci_probe_controller(pci_device_t* pci_dev) {
    if (g_controller_count >= AHCI_MAX_CONTROLLERS) return;

    uint32_t mmio_base;
    if (!ahci_resolve_bar5(pci_dev, &mmio_base)) {
        kklog("ahci: controller bar above 4gb, skipping");
        return;
    }

    uint32_t cmd = pci_config_read(pci_dev->bus, pci_dev->device, pci_dev->function, 0x04);
    cmd |= 0x06;
    cmd &= ~0x400u;
    pci_config_write(pci_dev->bus, pci_dev->device, pci_dev->function, 0x04, cmd);

    ahci_map_mmio(mmio_base);

    ahci_controller_t* ctrl = &g_controllers[g_controller_count];
    memset(ctrl, 0, sizeof(ahci_controller_t));
    ctrl->abar = (hba_mem_t*)(uintptr_t)mmio_base;

    ctrl->abar->ghc |= HBA_GHC_AE;

    uint32_t cap = ctrl->abar->cap;
    ctrl->n_ports_total = (cap & 0x1F) + 1;
    ctrl->n_cmd_slots = ((cap >> 8) & 0x1F) + 1;
    if (ctrl->n_cmd_slots > AHCI_CMD_SLOTS) ctrl->n_cmd_slots = AHCI_CMD_SLOTS;

    for (int i = 0; i < 32; i++) ctrl->port_to_dev[i] = -1;

    uint32_t pi = ctrl->abar->pi;
    for (int port_idx = 0; port_idx < 32; port_idx++) {
        if (!(pi & (1u << port_idx))) continue;
        hba_port_t* port = &ctrl->abar->ports[port_idx];
        if (!ahci_port_present(port)) continue;

        ahci_dev_type_t type = ahci_classify_sig(port->sig);
        if (type == AHCI_DEV_NONE) continue;

        if (ahci_init_port_region(ctrl, port_idx) != 0) continue;

        if (g_device_count >= AHCI_MAX_DEVICES) break;

        ahci_device_t* dev = &g_devices[g_device_count];
        memset(dev, 0, sizeof(ahci_device_t));
        dev->controller_index = g_controller_count;
        dev->port_index = port_idx;
        dev->type = type;
        dev->present = 1;

        if (ahci_identify_device(ctrl, port_idx, dev) != 0) {
            dev->present = 0;
            continue;
        }

        ctrl->port_to_dev[port_idx] = g_device_count;
        g_device_count++;
    }

    ctrl->valid = 1;
    g_controller_count++;
}

void ahci_init(void) {
    int index = 0;
    pci_device_t pci_dev;

    while (pci_find_class(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_SATA, &pci_dev, index) == 0) {
        uint32_t progif = (pci_config_read(pci_dev.bus, pci_dev.device, pci_dev.function, 0x08) >> 8) & 0xFF;
        if (progif == PCI_PROGIF_AHCI) {
            ahci_probe_controller(&pci_dev);
        }
        index++;
    }

    if (g_device_count == 0) {
        kklog("ahci: no sata/ata devices found");
    } else {
        kklogf("ahci: %d drive(s) ready\n", g_device_count);
    }
}

int ahci_device_count(void) {
    return g_device_count;
}

ahci_device_t* ahci_get_device(int index) {
    if (index < 0 || index >= g_device_count) return 0;
    return &g_devices[index];
}

static int ahci_lookup(int dev_index, ahci_controller_t** out_ctrl, int* out_port, ahci_device_t** out_dev) {
    if (dev_index < 0 || dev_index >= g_device_count) return -1;
    ahci_device_t* dev = &g_devices[dev_index];
    if (!dev->present) return -1;
    if (dev->controller_index < 0 || dev->controller_index >= g_controller_count) return -1;

    *out_ctrl = &g_controllers[dev->controller_index];
    *out_port = dev->port_index;
    *out_dev = dev;
    return 0;
}

int ahci_read_sectors(int dev_index, uint64_t lba, uint32_t count, void* buffer) {
    ahci_controller_t* ctrl;
    int port_idx;
    ahci_device_t* dev;
    if (ahci_lookup(dev_index, &ctrl, &port_idx, &dev) != 0) return -1;
    if (dev->type != AHCI_DEV_SATA) return -1;
    if (count == 0 || count > 65535) return -1;
    if (lba + count > dev->sector_count && dev->sector_count != 0) return -1;

    return ahci_run_command(ctrl, port_idx, ATA_CMD_READ_DMA_EXT, lba, count, buffer, 0);
}

int ahci_write_sectors(int dev_index, uint64_t lba, uint32_t count, const void* buffer) {
    ahci_controller_t* ctrl;
    int port_idx;
    ahci_device_t* dev;
    if (ahci_lookup(dev_index, &ctrl, &port_idx, &dev) != 0) return -1;
    if (dev->type != AHCI_DEV_SATA) return -1;
    if (count == 0 || count > 65535) return -1;
    if (lba + count > dev->sector_count && dev->sector_count != 0) return -1;

    return ahci_run_command(ctrl, port_idx, ATA_CMD_WRITE_DMA_EXT, lba, count, (void*)buffer, 1);
}

int ahci_identify(int dev_index, uint16_t* out_identify_data) {
    ahci_controller_t* ctrl;
    int port_idx;
    ahci_device_t* dev;
    if (ahci_lookup(dev_index, &ctrl, &port_idx, &dev) != 0) return -1;
    if (!out_identify_data) return -1;

    uint8_t cmd = (dev->type == AHCI_DEV_SATAPI) ? ATA_CMD_IDENTIFY_PACKET : ATA_CMD_IDENTIFY;
    return ahci_run_command(ctrl, port_idx, cmd, 0, 1, out_identify_data, 0);
}