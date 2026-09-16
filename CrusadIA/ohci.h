#ifndef OHCI_H
#define OHCI_H
#include <stdint.h>
#include "usb.h"

#define OHCI_REG_REVISION      0x00
#define OHCI_REG_CONTROL       0x01
#define OHCI_REG_CMDSTATUS     0x02
#define OHCI_REG_INTSTATUS     0x03
#define OHCI_REG_INTENABLE     0x04
#define OHCI_REG_INTDISABLE    0x05
#define OHCI_REG_HCCA          0x06
#define OHCI_REG_PERIODCURED   0x07
#define OHCI_REG_CTRLHEADED    0x08
#define OHCI_REG_CTRLCURED     0x09
#define OHCI_REG_BULKHEADED    0x0A
#define OHCI_REG_BULKCURED     0x0B
#define OHCI_REG_DONEHEAD      0x0C
#define OHCI_REG_FMINTERVAL    0x0D
#define OHCI_REG_FMREMAINING   0x0E
#define OHCI_REG_FMNUMBER      0x0F
#define OHCI_REG_PERIODICSTART 0x10
#define OHCI_REG_LSTHRESH      0x11
#define OHCI_REG_RHDESCA       0x12
#define OHCI_REG_RHDESCB       0x13
#define OHCI_REG_RHSTATUS      0x14
#define OHCI_REG_RHPORTSTATUS  0x15

#define OHCI_CTRL_CBSR_MASK    0x3
#define OHCI_CTRL_PLE          (1 << 2)
#define OHCI_CTRL_IE           (1 << 3)
#define OHCI_CTRL_CLE          (1 << 4)
#define OHCI_CTRL_BLE          (1 << 5)
#define OHCI_CTRL_HCFS_MASK    (0x3 << 6)
#define OHCI_CTRL_HCFS_RESET   (0x0 << 6)
#define OHCI_CTRL_HCFS_RESUME  (0x1 << 6)
#define OHCI_CTRL_HCFS_OPERATIONAL (0x2 << 6)
#define OHCI_CTRL_HCFS_SUSPEND (0x3 << 6)
#define OHCI_CTRL_IR           (1 << 8)

#define OHCI_CMDSTATUS_HCR     (1 << 0)
#define OHCI_CMDSTATUS_CLF     (1 << 1)
#define OHCI_CMDSTATUS_BLF     (1 << 2)
#define OHCI_CMDSTATUS_OCR     (1 << 3)

#define OHCI_RHPS_CCS          (1 << 0)
#define OHCI_RHPS_PES          (1 << 1)
#define OHCI_RHPS_PSS          (1 << 2)
#define OHCI_RHPS_POCI         (1 << 3)
#define OHCI_RHPS_PRS          (1 << 4)
#define OHCI_RHPS_PPS          (1 << 8)
#define OHCI_RHPS_LSDA         (1 << 9)
#define OHCI_RHPS_CSC          (1 << 16)
#define OHCI_RHPS_PESC         (1 << 17)
#define OHCI_RHPS_PSSC         (1 << 18)
#define OHCI_RHPS_OCIC         (1 << 19)
#define OHCI_RHPS_PRSC         (1 << 20)

typedef struct {
    volatile uint32_t hcca_interrupt_table[32];
    volatile uint16_t hcca_frame_number;
    volatile uint16_t hcca_pad1;
    volatile uint32_t hcca_done_head;
    volatile uint8_t reserved[116];
} __attribute__((packed, aligned(256))) ohci_hcca_t;

typedef struct ohci_td {
    volatile uint32_t control;
    volatile uint32_t cbp;
    volatile uint32_t next_td;
    volatile uint32_t buffer_end;
    void* usb_dev;
    uint8_t is_interrupt;
    uint8_t pad[3];
} __attribute__((aligned(32))) ohci_td_t;

typedef struct ohci_ed {
    volatile uint32_t control;
    volatile uint32_t tail_td;
    volatile uint32_t head_td;
    volatile uint32_t next_ed;
} __attribute__((aligned(16))) ohci_ed_t;

#define OHCI_TD_CC_MASK        (0xF << 28)
#define OHCI_TD_CC_SHIFT       28
#define OHCI_TD_EC_MASK        (0x3 << 26)
#define OHCI_TD_T_MASK         (0x3 << 24)
#define OHCI_TD_T_DATA0        (0x2 << 24)
#define OHCI_TD_T_DATA1        (0x3 << 24)
#define OHCI_TD_DI_MASK        (0x7 << 21)
#define OHCI_TD_DI_IMMEDIATE   (0x0 << 21)
#define OHCI_TD_DP_SETUP       (0x0 << 19)
#define OHCI_TD_DP_OUT         (0x1 << 19)
#define OHCI_TD_DP_IN          (0x2 << 19)
#define OHCI_TD_R              (1 << 18)

#define OHCI_ED_FA_SHIFT       0
#define OHCI_ED_EN_SHIFT       7
#define OHCI_ED_DIR_SHIFT      11
#define OHCI_ED_DIR_TD         (0x0 << 11)
#define OHCI_ED_DIR_OUT        (0x1 << 11)
#define OHCI_ED_DIR_IN         (0x2 << 11)
#define OHCI_ED_SPEED_LOW      (1 << 13)
#define OHCI_ED_SKIP           (1 << 14)
#define OHCI_ED_FORMAT_ISO     (1 << 15)
#define OHCI_ED_MPS_SHIFT      16

usb_hcd_t* ohci_probe_and_init(uint32_t bar0_phys);
void ohci_poll_interrupts(void);

#endif
