#include "ehci.h"
#include "usb.h"
#include "pmm.h"
#include "pci.h"
#include "klog.h"
#include <string.h>

#define EHCI_MAX_HC 4
#define EHCI_TIMEOUT_LOOPS 2000000
#define EHCI_PERIODIC_ENTRIES 1024

typedef struct {
    volatile uint8_t* base;
    volatile uint32_t* op;
    uint8_t caplength;
    int n_ports;
    ehci_qh_t* async_head;
    uint32_t* periodic_list;
    usb_hcd_t hcd;
    void (*release_port_to_companion)(int port);
} ehci_hc_t;

static ehci_hc_t g_hc[EHCI_MAX_HC];
static int g_hc_count = 0;

static inline uint32_t ehci_op_read(ehci_hc_t* hc, int reg) {
    return *(volatile uint32_t*)(hc->base + hc->caplength + reg);
}

static inline void ehci_op_write(ehci_hc_t* hc, int reg, uint32_t val) {
    *(volatile uint32_t*)(hc->base + hc->caplength + reg) = val;
}

static inline uint32_t ehci_cap_read32(ehci_hc_t* hc, int reg) {
    return *(volatile uint32_t*)(hc->base + reg);
}

static inline uint16_t ehci_cap_read16(ehci_hc_t* hc, int reg) {
    return *(volatile uint16_t*)(hc->base + reg);
}

static void ehci_delay(int loops) {
    for (volatile int i = 0; i < loops; i++) {
        asm volatile("nop");
    }
}

static ehci_qtd_t* ehci_alloc_qtd(void) {
    static ehci_qtd_t* pool = 0;
    static int used = 0;
    if (!pool || used >= (4096 / (int)sizeof(ehci_qtd_t))) {
        pool = (ehci_qtd_t*)pmm_alloc_block();
        memset(pool, 0, 4096);
        used = 0;
    }
    ehci_qtd_t* t = &pool[used++];
    memset(t, 0, sizeof(ehci_qtd_t));
    return t;
}

static ehci_qh_t* ehci_alloc_qh(void) {
    static ehci_qh_t* pool = 0;
    static int used = 0;
    if (!pool || used >= (4096 / (int)sizeof(ehci_qh_t))) {
        pool = (ehci_qh_t*)pmm_alloc_block();
        memset(pool, 0, 4096);
        used = 0;
    }
    ehci_qh_t* q = &pool[used++];
    memset(q, 0, sizeof(ehci_qh_t));
    return q;
}

static void ehci_fill_qtd(ehci_qtd_t* td, int pid, int toggle, void* buf, int len, int ioc) {
    td->next_qtd = EHCI_LP_TERMINATE;
    td->alt_next_qtd = EHCI_LP_TERMINATE;
    uint32_t token = (len << 16) | (toggle << 31) | (pid << 8) | EHCI_QTD_STATUS_ACTIVE;
    if (ioc) token |= (1 << 15);
    token |= (3 << 10);
    td->token = token;
    if (buf) {
        uint32_t addr = (uint32_t)(uintptr_t)buf;
        for (int i = 0; i < 5; i++) {
            td->buffer[i] = (addr & ~0xFFF) + i * 4096;
        }
        td->buffer[0] = addr;
    }
}

static int ehci_wait_qtd(ehci_qtd_t* td) {
    int loops = EHCI_TIMEOUT_LOOPS;
    while (loops--) {
        uint32_t tok = td->token;
        if (!(tok & EHCI_QTD_STATUS_ACTIVE)) {
            if (tok & (EHCI_QTD_STATUS_HALTED | EHCI_QTD_STATUS_BUFERR | EHCI_QTD_STATUS_BABBLE | EHCI_QTD_STATUS_XACTERR)) {
                return -1;
            }
            return 0;
        }
        asm volatile("pause");
    }
    return -2;
}

static int ehci_do_control(usb_hcd_t* hcdp, usb_device_t* dev, usb_setup_pkt_t* setup, void* buf, int len, int dir_in) {
    ehci_hc_t* hc = (ehci_hc_t*)hcdp->priv;

    ehci_qtd_t* setup_td = ehci_alloc_qtd();
    ehci_qtd_t* data_td = (len > 0) ? ehci_alloc_qtd() : 0;
    ehci_qtd_t* status_td = ehci_alloc_qtd();

    ehci_fill_qtd(setup_td, EHCI_QTD_PID_SETUP, 0, setup, sizeof(usb_setup_pkt_t), 0);
    if (data_td) {
        ehci_fill_qtd(data_td, dir_in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT, 1, buf, len, 0);
        setup_td->next_qtd = (uint32_t)(uintptr_t)data_td;
        ehci_fill_qtd(status_td, dir_in ? EHCI_QTD_PID_OUT : EHCI_QTD_PID_IN, 1, 0, 0, 1);
        data_td->next_qtd = (uint32_t)(uintptr_t)status_td;
    } else {
        ehci_fill_qtd(status_td, dir_in ? EHCI_QTD_PID_OUT : EHCI_QTD_PID_IN, 1, 0, 0, 1);
        setup_td->next_qtd = (uint32_t)(uintptr_t)status_td;
    }

    ehci_qh_t* qh = ehci_alloc_qh();
    int speed_field = dev->speed == 2 ? EHCI_QH_EPCHAR_EPS_HIGH : (dev->speed == 1 ? EHCI_QH_EPCHAR_EPS_LOW : EHCI_QH_EPCHAR_EPS_FULL);
    qh->ep_char = (dev->address << EHCI_QH_EPCHAR_DEVADDR_SHIFT) | (0 << EHCI_QH_EPCHAR_EP_SHIFT) |
                  (speed_field << EHCI_QH_EPCHAR_EPS_SHIFT) | EHCI_QH_EPCHAR_DTC |
                  (dev->max_packet0 << EHCI_QH_EPCHAR_MPL_SHIFT) | EHCI_QH_EPCHAR_CONTROL_EP |
                  (0 << EHCI_QH_EPCHAR_NAK_RL_SHIFT);
    qh->ep_caps = (1 << EHCI_QH_CAPS_MULT_SHIFT);
    qh->current_qtd = 0;
    qh->next_qtd = (uint32_t)(uintptr_t)setup_td;
    qh->alt_next_qtd = EHCI_LP_TERMINATE;
    qh->token = 0;

    qh->horiz_link = hc->async_head->horiz_link;
    hc->async_head->horiz_link = (uint32_t)(uintptr_t)qh | EHCI_LP_TYPE_QH;

    ehci_op_write(hc, EHCI_OP_USBCMD, ehci_op_read(hc, EHCI_OP_USBCMD) | EHCI_CMD_ASE);
    int loops = EHCI_TIMEOUT_LOOPS;
    while (loops-- && !(ehci_op_read(hc, EHCI_OP_USBSTS) & EHCI_STS_ASS)) {
        asm volatile("pause");
    }

    int r = ehci_wait_qtd(status_td);

    hc->async_head->horiz_link = qh->horiz_link;

    if (r != 0) return -1;
    return len;
}

typedef struct {
    usb_device_t* dev;
    void (*callback)(usb_device_t*, uint8_t*, int);
    uint8_t* buf;
    int maxpkt;
    ehci_qh_t* qh;
    ehci_qtd_t* td;
    int toggle;
} ehci_intr_ctx_t;

static ehci_intr_ctx_t g_intr_ctx[16];
static int g_intr_ctx_count = 0;
static uint8_t g_intr_bufs[16][64];

static int ehci_setup_interrupt_in(usb_hcd_t* hcdp, usb_device_t* dev, uint8_t ep_addr, uint16_t maxpkt, uint8_t interval, void (*callback)(usb_device_t*, uint8_t*, int)) {
    ehci_hc_t* hc = (ehci_hc_t*)hcdp->priv;
    if (g_intr_ctx_count >= 16) return -1;

    uint8_t* buf = g_intr_bufs[g_intr_ctx_count];
    ehci_qtd_t* td = ehci_alloc_qtd();
    ehci_fill_qtd(td, EHCI_QTD_PID_IN, 0, buf, maxpkt, 1);

    ehci_qh_t* qh = ehci_alloc_qh();
    int speed_field = dev->speed == 2 ? EHCI_QH_EPCHAR_EPS_HIGH : (dev->speed == 1 ? EHCI_QH_EPCHAR_EPS_LOW : EHCI_QH_EPCHAR_EPS_FULL);
    qh->ep_char = (dev->address << EHCI_QH_EPCHAR_DEVADDR_SHIFT) | ((ep_addr & 0xF) << EHCI_QH_EPCHAR_EP_SHIFT) |
                  (speed_field << EHCI_QH_EPCHAR_EPS_SHIFT) | EHCI_QH_EPCHAR_DTC |
                  (maxpkt << EHCI_QH_EPCHAR_MPL_SHIFT);
    qh->ep_caps = (1 << EHCI_QH_CAPS_MULT_SHIFT) | 0x1;
    qh->current_qtd = 0;
    qh->next_qtd = (uint32_t)(uintptr_t)td;
    qh->alt_next_qtd = EHCI_LP_TERMINATE;
    qh->horiz_link = EHCI_LP_TERMINATE;
    qh->token = 0;

    for (int i = 0; i < EHCI_PERIODIC_ENTRIES; i++) {
        hc->periodic_list[i] = (uint32_t)(uintptr_t)qh | EHCI_LP_TYPE_QH;
    }

    ehci_op_write(hc, EHCI_OP_USBCMD, ehci_op_read(hc, EHCI_OP_USBCMD) | EHCI_CMD_PSE);

    ehci_intr_ctx_t* ctx = &g_intr_ctx[g_intr_ctx_count++];
    ctx->dev = dev;
    ctx->callback = callback;
    ctx->buf = buf;
    ctx->maxpkt = maxpkt;
    ctx->qh = qh;
    ctx->td = td;
    ctx->toggle = 0;
    return 0;
}

void ehci_poll_interrupts(void) {
    for (int i = 0; i < g_intr_ctx_count; i++) {
        ehci_intr_ctx_t* ctx = &g_intr_ctx[i];
        uint32_t tok = ctx->td->token;
        if (!(tok & EHCI_QTD_STATUS_ACTIVE)) {
            int cc_ok = !(tok & (EHCI_QTD_STATUS_HALTED | EHCI_QTD_STATUS_BUFERR | EHCI_QTD_STATUS_BABBLE | EHCI_QTD_STATUS_XACTERR));
            int remaining = (tok >> 16) & 0x7FFF;
            int actual = ctx->maxpkt - remaining;
            if (cc_ok && actual > 0) {
                ctx->callback(ctx->dev, ctx->buf, actual);
            }
            if (cc_ok) {
                ctx->toggle ^= 1;
            }
            ehci_fill_qtd(ctx->td, EHCI_QTD_PID_IN, ctx->toggle, ctx->buf, ctx->maxpkt, 1);
            ctx->qh->next_qtd = (uint32_t)(uintptr_t)ctx->td;
            ctx->qh->token = 0;
        }
    }
}

static void ehci_bios_handoff(uint8_t bus, uint8_t dev, uint8_t func, uint32_t hccparams) {
    uint32_t eecp = (hccparams >> 8) & 0xFF;
    if (eecp < 0x40) return;
    uint32_t cap = pci_config_read(bus, dev, func, eecp);
    if ((cap & 0xFF) != 0x01) return;
    uint32_t legsup = cap;
    if (legsup & (1 << 16)) {
        pci_config_write(bus, dev, func, eecp, legsup | (1 << 24));
        int loops = 1000000;
        while (loops--) {
            uint32_t v = pci_config_read(bus, dev, func, eecp);
            if ((v & (1 << 24)) && !(v & (1 << 16))) break;
            asm volatile("pause");
        }
    }
}

static void ehci_reset_and_route_port(ehci_hc_t* hc, int port) {
    uint32_t status = ehci_op_read(hc, EHCI_OP_PORTSC + port * 4);
    if (!(status & EHCI_PORTSC_CCS)) return;

    status &= ~EHCI_PORTSC_PED;
    status |= EHCI_PORTSC_PR;
    status &= ~(EHCI_PORTSC_CSC | EHCI_PORTSC_PEDC);
    ehci_op_write(hc, EHCI_OP_PORTSC + port * 4, status);
    ehci_delay(3000000);

    status = ehci_op_read(hc, EHCI_OP_PORTSC + port * 4);
    status &= ~EHCI_PORTSC_PR;
    ehci_op_write(hc, EHCI_OP_PORTSC + port * 4, status);
    ehci_delay(200000);

    status = ehci_op_read(hc, EHCI_OP_PORTSC + port * 4);
    if (status & EHCI_PORTSC_PED) {
        usb_enumerate_device(&hc->hcd, 0, port, 2);
    } else if (status & EHCI_PORTSC_CCS) {
        ehci_op_write(hc, EHCI_OP_PORTSC + port * 4, status | EHCI_PORTSC_PO);
        if (hc->release_port_to_companion) {
            hc->release_port_to_companion(port);
        }
    }
}

usb_hcd_t* ehci_probe_and_init(uint32_t bar0_phys, void (*release_port_to_companion)(int port)) {
    if (g_hc_count >= EHCI_MAX_HC) return 0;
    ehci_hc_t* hc = &g_hc[g_hc_count++];
    memset(hc, 0, sizeof(ehci_hc_t));
    hc->base = (volatile uint8_t*)(uintptr_t)(bar0_phys & ~0xF);
    hc->caplength = *(volatile uint8_t*)(hc->base + EHCI_CAP_CAPLENGTH);
    hc->release_port_to_companion = release_port_to_companion;

    uint32_t hcsparams = ehci_cap_read32(hc, EHCI_CAP_HCSPARAMS);
    hc->n_ports = hcsparams & 0xF;
    if (hc->n_ports > USB_MAX_HUB_PORTS) hc->n_ports = USB_MAX_HUB_PORTS;

    ehci_op_write(hc, EHCI_OP_USBCMD, ehci_op_read(hc, EHCI_OP_USBCMD) & ~EHCI_CMD_RUN);
    int loops = EHCI_TIMEOUT_LOOPS;
    while (loops-- && !(ehci_op_read(hc, EHCI_OP_USBSTS) & EHCI_STS_HCHALTED)) {
        asm volatile("pause");
    }

    ehci_op_write(hc, EHCI_OP_USBCMD, EHCI_CMD_HCRESET);
    loops = EHCI_TIMEOUT_LOOPS;
    while (loops-- && (ehci_op_read(hc, EHCI_OP_USBCMD) & EHCI_CMD_HCRESET)) {
        asm volatile("pause");
    }

    hc->periodic_list = (uint32_t*)pmm_alloc_block();
    for (int i = 0; i < EHCI_PERIODIC_ENTRIES; i++) {
        hc->periodic_list[i] = EHCI_LP_TERMINATE;
    }

    hc->async_head = ehci_alloc_qh();
    hc->async_head->horiz_link = (uint32_t)(uintptr_t)hc->async_head | EHCI_LP_TYPE_QH;
    hc->async_head->ep_char = EHCI_QH_EPCHAR_H | (0 << EHCI_QH_EPCHAR_EPS_SHIFT);
    hc->async_head->ep_caps = 0;
    hc->async_head->current_qtd = 0;
    hc->async_head->next_qtd = EHCI_LP_TERMINATE;
    hc->async_head->alt_next_qtd = EHCI_LP_TERMINATE;
    hc->async_head->token = EHCI_QTD_STATUS_HALTED;

    ehci_op_write(hc, EHCI_OP_CTRLDSSEGMENT, 0);
    ehci_op_write(hc, EHCI_OP_PERIODICLISTBASE, (uint32_t)(uintptr_t)hc->periodic_list);
    ehci_op_write(hc, EHCI_OP_ASYNCLISTADDR, (uint32_t)(uintptr_t)hc->async_head);
    ehci_op_write(hc, EHCI_OP_USBINTR, 0);

    ehci_op_write(hc, EHCI_OP_USBCMD, EHCI_CMD_RUN | EHCI_CMD_ASE | EHCI_CMD_PSE | EHCI_CMD_ITC_1);
    ehci_op_write(hc, EHCI_OP_CONFIGFLAG, 1);
    ehci_delay(1000000);

    hc->hcd.control_transfer = ehci_do_control;
    hc->hcd.setup_interrupt_in = ehci_setup_interrupt_in;
    hc->hcd.priv = hc;

    klog_status("EHCI CONTROLLER STARTED", 0x00FF00);

    for (int port = 0; port < hc->n_ports; port++) {
        ehci_reset_and_route_port(hc, port);
    }

    return &hc->hcd;
}

void ehci_bios_handoff_pci(uint8_t bus, uint8_t dev, uint8_t func, uint32_t bar0_phys) {
    volatile uint8_t* base = (volatile uint8_t*)(uintptr_t)(bar0_phys & ~0xF);
    uint32_t hccparams = *(volatile uint32_t*)(base + EHCI_CAP_HCCPARAMS);
    ehci_bios_handoff(bus, dev, func, hccparams);
}