#ifndef EHCI_H
#define EHCI_H
#include <stdint.h>
#include "usb.h"

#define EHCI_CAP_CAPLENGTH   0x00
#define EHCI_CAP_HCIVERSION  0x02
#define EHCI_CAP_HCSPARAMS   0x04
#define EHCI_CAP_HCCPARAMS   0x08

#define EHCI_OP_USBCMD       0x00
#define EHCI_OP_USBSTS       0x04
#define EHCI_OP_USBINTR      0x08
#define EHCI_OP_FRINDEX      0x0C
#define EHCI_OP_CTRLDSSEGMENT 0x10
#define EHCI_OP_PERIODICLISTBASE 0x14
#define EHCI_OP_ASYNCLISTADDR 0x18
#define EHCI_OP_CONFIGFLAG   0x40
#define EHCI_OP_PORTSC       0x44

#define EHCI_CMD_RUN          (1 << 0)
#define EHCI_CMD_HCRESET      (1 << 1)
#define EHCI_CMD_PSE          (1 << 4)
#define EHCI_CMD_ASE          (1 << 5)
#define EHCI_CMD_IOAAD        (1 << 6)
#define EHCI_CMD_ITC_1        (1 << 16)

#define EHCI_STS_USBINT       (1 << 0)
#define EHCI_STS_USBERRINT    (1 << 1)
#define EHCI_STS_PCD          (1 << 2)
#define EHCI_STS_HCHALTED     (1 << 12)
#define EHCI_STS_ASS          (1 << 15)
#define EHCI_STS_PSS          (1 << 14)

#define EHCI_PORTSC_CCS        (1 << 0)
#define EHCI_PORTSC_CSC        (1 << 1)
#define EHCI_PORTSC_PED        (1 << 2)
#define EHCI_PORTSC_PEDC       (1 << 3)
#define EHCI_PORTSC_PR         (1 << 8)
#define EHCI_PORTSC_LINESTATUS_MASK (0x3 << 10)
#define EHCI_PORTSC_PP         (1 << 12)
#define EHCI_PORTSC_PO         (1 << 13)
#define EHCI_PORTSC_WKCNNT_E   (1 << 20)

#define EHCI_QTD_STATUS_ACTIVE      (1 << 7)
#define EHCI_QTD_STATUS_HALTED      (1 << 6)
#define EHCI_QTD_STATUS_BUFERR      (1 << 5)
#define EHCI_QTD_STATUS_BABBLE      (1 << 4)
#define EHCI_QTD_STATUS_XACTERR     (1 << 3)
#define EHCI_QTD_STATUS_MISSEDMICRO (1 << 2)

#define EHCI_QTD_PID_OUT   0x0
#define EHCI_QTD_PID_IN    0x1
#define EHCI_QTD_PID_SETUP 0x2

typedef struct ehci_qtd {
    volatile uint32_t next_qtd;
    volatile uint32_t alt_next_qtd;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
    void* backing;
    uint32_t pad[3];
} __attribute__((aligned(32))) ehci_qtd_t;

typedef struct ehci_qh {
    volatile uint32_t horiz_link;
    volatile uint32_t ep_char;
    volatile uint32_t ep_caps;
    volatile uint32_t current_qtd;
    volatile uint32_t next_qtd;
    volatile uint32_t alt_next_qtd;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
} __attribute__((aligned(32))) ehci_qh_t;

#define EHCI_QH_EPCHAR_DEVADDR_SHIFT 0
#define EHCI_QH_EPCHAR_EP_SHIFT      8
#define EHCI_QH_EPCHAR_EPS_SHIFT     12
#define EHCI_QH_EPCHAR_EPS_FULL      0x0
#define EHCI_QH_EPCHAR_EPS_LOW       0x1
#define EHCI_QH_EPCHAR_EPS_HIGH      0x2
#define EHCI_QH_EPCHAR_DTC           (1 << 14)
#define EHCI_QH_EPCHAR_H             (1 << 15)
#define EHCI_QH_EPCHAR_MPL_SHIFT     16
#define EHCI_QH_EPCHAR_CONTROL_EP    (1 << 27)
#define EHCI_QH_EPCHAR_NAK_RL_SHIFT  28

#define EHCI_QH_CAPS_MULT_SHIFT      30

#define EHCI_LP_TYPE_QH  (1 << 1)
#define EHCI_LP_TERMINATE 0x1

usb_hcd_t* ehci_probe_and_init(uint32_t bar0_phys, void (*release_port_to_companion)(int port));
void ehci_poll_interrupts(void);

#endif