#ifndef XHCI_H
#define XHCI_H
#include <stdint.h>
#include "usb.h"

/* Capability registers (offsets from BAR0) */
#define XHCI_CAP_CAPLENGTH   0x00
#define XHCI_CAP_HCIVERSION  0x02
#define XHCI_CAP_HCSPARAMS1  0x04
#define XHCI_CAP_HCSPARAMS2  0x08
#define XHCI_CAP_HCSPARAMS3  0x0C
#define XHCI_CAP_HCCPARAMS1  0x10
#define XHCI_CAP_DBOFF       0x14
#define XHCI_CAP_RTSOFF      0x18

/* Operational registers (offset from BAR0 + CAPLENGTH) */
#define XHCI_OP_USBCMD       0x00
#define XHCI_OP_USBSTS       0x04
#define XHCI_OP_PAGESIZE     0x08
#define XHCI_OP_DNCTRL       0x14
#define XHCI_OP_CRCR_LO      0x18
#define XHCI_OP_CRCR_HI      0x1C
#define XHCI_OP_DCBAAP_LO    0x30
#define XHCI_OP_DCBAAP_HI    0x34
#define XHCI_OP_CONFIG       0x38
#define XHCI_OP_PORTSC_BASE  0x400

#define XHCI_CMD_RUN          (1 << 0)
#define XHCI_CMD_HCRESET      (1 << 1)
#define XHCI_CMD_INTE         (1 << 2)

#define XHCI_STS_HCHALTED     (1 << 0)
#define XHCI_STS_CNR          (1 << 11)

#define XHCI_CRCR_RCS         (1 << 0)

#define XHCI_PORTSC_CCS        (1u << 0)
#define XHCI_PORTSC_PED        (1u << 1)
#define XHCI_PORTSC_PR         (1u << 4)
#define XHCI_PORTSC_PLS_SHIFT  5
#define XHCI_PORTSC_PP         (1u << 9)
#define XHCI_PORTSC_SPEED_SHIFT 10
#define XHCI_PORTSC_SPEED_MASK  0xF
#define XHCI_PORTSC_CSC        (1u << 17)
#define XHCI_PORTSC_PEC        (1u << 18)
#define XHCI_PORTSC_PRC        (1u << 21)
#define XHCI_PORTSC_RW1CS      (XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | (1u<<19) | (1u<<20) | XHCI_PORTSC_PRC | (1u<<22) | (1u<<23))

/* Runtime registers, relative to RTSOFF; interrupter 0 starts at +0x20 */
#define XHCI_RT_IR0            0x20
#define XHCI_IR_IMAN           0x00
#define XHCI_IR_IMOD           0x04
#define XHCI_IR_ERSTSZ         0x08
#define XHCI_IR_ERSTBA_LO      0x10
#define XHCI_IR_ERSTBA_HI      0x14
#define XHCI_IR_ERDP_LO        0x18
#define XHCI_IR_ERDP_HI        0x1C

/* TRB */
typedef struct {
    volatile uint32_t p_lo;
    volatile uint32_t p_hi;
    volatile uint32_t status;
    volatile uint32_t control;
} __attribute__((aligned(16))) xhci_trb_t;

#define XHCI_TRB_CYCLE         (1u << 0)
#define XHCI_TRB_ENT           (1u << 1)
#define XHCI_TRB_ISP           (1u << 2)
#define XHCI_TRB_IOC           (1u << 5)
#define XHCI_TRB_IDT           (1u << 6)
#define XHCI_TRB_TC            (1u << 1)
#define XHCI_TRB_TYPE_SHIFT    10
#define XHCI_TRB_DIR_IN        (1u << 16)

#define XHCI_TRB_TYPE_NORMAL         1
#define XHCI_TRB_TYPE_SETUP_STAGE    2
#define XHCI_TRB_TYPE_DATA_STAGE     3
#define XHCI_TRB_TYPE_STATUS_STAGE   4
#define XHCI_TRB_TYPE_LINK           6
#define XHCI_TRB_TYPE_ENABLE_SLOT    9
#define XHCI_TRB_TYPE_DISABLE_SLOT   10
#define XHCI_TRB_TYPE_ADDRESS_DEVICE 11
#define XHCI_TRB_TYPE_CONFIG_EP      12
#define XHCI_TRB_TYPE_NOOP_CMD       23
#define XHCI_TRB_TYPE_TRANSFER_EVENT 32
#define XHCI_TRB_TYPE_CMD_COMPLETION 33
#define XHCI_TRB_TYPE_PORT_STATUS_CHANGE 34
#define XHCI_TRB_TYPE_NOOP_TRANSFER  8

#define XHCI_TRB_BSR           (1u << 9) /* Address Device: Block Set Address Request */

#define XHCI_COMP_SUCCESS 1

usb_hcd_t* xhci_probe_and_init(uint8_t bus, uint8_t dev, uint8_t func, uint32_t bar0_phys);
void xhci_poll_interrupts(void);
void xhci_bios_handoff_pci(uint32_t bar0_phys);

#endif
