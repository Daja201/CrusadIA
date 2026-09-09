#include "ohci.h"
#include "usb.h"
#include "pmm.h"
#include "klog.h"
#include <string.h>

#define OHCI_MAX_HC 4
#define OHCI_TIMEOUT_LOOPS 2000000

typedef struct {
    volatile uint32_t* regs;
    ohci_hcca_t* hcca;
    ohci_ed_t* ctrl_ed_pool[USB_MAX_DEVICES];
    ohci_ed_t* intr_ed_pool[16];
    int intr_ed_count;
    usb_hcd_t hcd;
} ohci_hc_t;

static ohci_hc_t g_hc[OHCI_MAX_HC];
static int g_hc_count = 0;

static inline uint32_t ohci_read(ohci_hc_t* hc, int reg) {
    return hc->regs[reg];
}

static inline void ohci_write(ohci_hc_t* hc, int reg, uint32_t val) {
    hc->regs[reg] = val;
}

static void* ohci_alloc_aligned(int size) {
    void* p = pmm_alloc_block();
    if (!p) return 0;
    memset(p, 0, size > 4096 ? 4096 : 4096);
    return p;
}

static ohci_td_t* ohci_alloc_td(void) {
    static ohci_td_t* pool = 0;
    static int pool_used = 0;
    if (!pool || pool_used >= (4096 / (int)sizeof(ohci_td_t))) {
        pool = (ohci_td_t*)pmm_alloc_block();
        memset(pool, 0, 4096);
        pool_used = 0;
    }
    ohci_td_t* td = &pool[pool_used++];
    memset(td, 0, sizeof(ohci_td_t));
    return td;
}

static ohci_ed_t* ohci_alloc_ed(void) {
    static ohci_ed_t* pool = 0;
    static int pool_used = 0;
    if (!pool || pool_used >= (4096 / (int)sizeof(ohci_ed_t))) {
        pool = (ohci_ed_t*)pmm_alloc_block();
        memset(pool, 0, 4096);
        pool_used = 0;
    }
    ohci_ed_t* ed = &pool[pool_used++];
    memset(ed, 0, sizeof(ohci_ed_t));
    return ed;
}

static int ohci_wait_td_done(ohci_ed_t* ed, ohci_td_t* tail_marker) {
    int loops = OHCI_TIMEOUT_LOOPS;
    while (loops--) {
        uint32_t head = ed->head_td & ~0xF;
        if (head == (uint32_t)(uintptr_t)tail_marker) {
            return 0;
        }
        if (ed->head_td & 0x1) {
            return -1;
        }
        asm volatile("pause");
    }
    return -2;
}

static int ohci_do_control(usb_hcd_t* hcdp, usb_device_t* dev, usb_setup_pkt_t* setup, void* buf, int len, int dir_in) {
    ohci_hc_t* hc = (ohci_hc_t*)hcdp->priv;
    if (!dev->hcd_priv) {
        dev->hcd_priv = ohci_alloc_ed();
    }
    ohci_ed_t* ed = (ohci_ed_t*)dev->hcd_priv;

    ohci_td_t* setup_td = ohci_alloc_td();
    ohci_td_t* data_td = (len > 0) ? ohci_alloc_td() : 0;
    ohci_td_t* status_td = ohci_alloc_td();

    setup_td->control = OHCI_TD_CC_MASK | OHCI_TD_DP_SETUP | OHCI_TD_DI_IMMEDIATE | OHCI_TD_T_DATA0;
    setup_td->cbp = (uint32_t)(uintptr_t)setup;
    setup_td->buffer_end = (uint32_t)(uintptr_t)setup + sizeof(usb_setup_pkt_t) - 1;
    setup_td->next_td = (uint32_t)(uintptr_t)(data_td ? data_td : status_td);

    if (data_td) {
        data_td->control = OHCI_TD_CC_MASK | (dir_in ? OHCI_TD_DP_IN : OHCI_TD_DP_OUT) | OHCI_TD_DI_IMMEDIATE | OHCI_TD_T_DATA1 | OHCI_TD_R;
        data_td->cbp = (uint32_t)(uintptr_t)buf;
        data_td->buffer_end = (uint32_t)(uintptr_t)buf + len - 1;
        data_td->next_td = (uint32_t)(uintptr_t)status_td;
    }

    status_td->control = OHCI_TD_CC_MASK | (dir_in ? OHCI_TD_DP_OUT : OHCI_TD_DP_IN) | OHCI_TD_DI_IMMEDIATE | OHCI_TD_T_DATA1;
    status_td->cbp = 0;
    status_td->buffer_end = 0;
    status_td->next_td = 0;

    ohci_td_t* tail = ohci_alloc_td();
    status_td->next_td = (uint32_t)(uintptr_t)tail;

    ed->control = (dev->address << OHCI_ED_FA_SHIFT) | (0 << OHCI_ED_EN_SHIFT) |
                  OHCI_ED_DIR_TD | (dev->max_packet0 << OHCI_ED_MPS_SHIFT);
    if (dev->speed == 1) ed->control |= OHCI_ED_SPEED_LOW;
    ed->head_td = (uint32_t)(uintptr_t)setup_td;
    ed->tail_td = (uint32_t)(uintptr_t)tail;
    ed->next_ed = 0;

    ohci_write(hc, OHCI_REG_CTRLHEADED, (uint32_t)(uintptr_t)ed);
    ohci_write(hc, OHCI_REG_CONTROL, ohci_read(hc, OHCI_REG_CONTROL) | OHCI_CTRL_CLE);
    ohci_write(hc, OHCI_REG_CMDSTATUS, ohci_read(hc, OHCI_REG_CMDSTATUS) | OHCI_CMDSTATUS_CLF);

    int r = ohci_wait_td_done(ed, tail);

    ohci_write(hc, OHCI_REG_CONTROL, ohci_read(hc, OHCI_REG_CONTROL) & ~OHCI_CTRL_CLE);
    ohci_write(hc, OHCI_REG_CTRLHEADED, 0);

    if (r != 0) return -1;
    return len;
}

typedef struct {
    usb_device_t* dev;
    void (*callback)(usb_device_t*, uint8_t*, int);
    uint8_t* buf;
    int maxpkt;
    ohci_ed_t* ed;
    ohci_td_t* td;
} ohci_intr_ctx_t;

static ohci_intr_ctx_t g_intr_ctx[16];
static int g_intr_ctx_count = 0;

static int ohci_setup_interrupt_in(usb_hcd_t* hcdp, usb_device_t* dev, uint8_t ep_addr, uint16_t maxpkt, uint8_t interval, void (*callback)(usb_device_t*, uint8_t*, int)) {
    ohci_hc_t* hc = (ohci_hc_t*)hcdp->priv;
    if (g_intr_ctx_count >= 16) return -1;

    ohci_ed_t* ed = ohci_alloc_ed();
    ohci_td_t* td = ohci_alloc_td();
    ohci_td_t* tail = ohci_alloc_td();

    static uint8_t intr_bufs[16][64];
    uint8_t* buf = intr_bufs[g_intr_ctx_count];

    td->control = OHCI_TD_CC_MASK | OHCI_TD_DP_IN | OHCI_TD_DI_IMMEDIATE | OHCI_TD_T_DATA0 | OHCI_TD_R;
    td->cbp = (uint32_t)(uintptr_t)buf;
    td->buffer_end = (uint32_t)(uintptr_t)buf + maxpkt - 1;
    td->next_td = (uint32_t)(uintptr_t)tail;

    ed->control = (dev->address << OHCI_ED_FA_SHIFT) | ((ep_addr & 0xF) << OHCI_ED_EN_SHIFT) |
                  OHCI_ED_DIR_IN | (maxpkt << OHCI_ED_MPS_SHIFT);
    if (dev->speed == 1) ed->control |= OHCI_ED_SPEED_LOW;
    ed->head_td = (uint32_t)(uintptr_t)td;
    ed->tail_td = (uint32_t)(uintptr_t)tail;
    ed->next_ed = 0;
    for (int i = 0; i < 32; i++) {
        uint32_t existing_raw = hc->hcca->hcca_interrupt_table[i];
        if (existing_raw == 0) {
            hc->hcca->hcca_interrupt_table[i] = (uint32_t)(uintptr_t)ed;
        } else {
            ohci_ed_t* walk = (ohci_ed_t*)(uintptr_t)existing_raw;
            while (walk->next_ed != 0 && (ohci_ed_t*)(uintptr_t)walk->next_ed != ed) {
                walk = (ohci_ed_t*)(uintptr_t)walk->next_ed;
            }
            if (walk != ed) {
                walk->next_ed = (uint32_t)(uintptr_t)ed;
            }
        }
    }
    ohci_write(hc, OHCI_REG_CONTROL, ohci_read(hc, OHCI_REG_CONTROL) | OHCI_CTRL_PLE);

    ohci_intr_ctx_t* ctx = &g_intr_ctx[g_intr_ctx_count++];
    ctx->dev = dev;
    ctx->callback = callback;
    ctx->buf = buf;
    ctx->maxpkt = maxpkt;
    ctx->ed = ed;
    ctx->td = td;
    return 0;
}

void ohci_poll_interrupts(void) {
    for (int i = 0; i < g_intr_ctx_count; i++) {
        ohci_intr_ctx_t* ctx = &g_intr_ctx[i];
        uint32_t head = ctx->ed->head_td & ~0xF;
        if (head != (uint32_t)(uintptr_t)ctx->td) {
            int cc = (ctx->td->control & OHCI_TD_CC_MASK) >> OHCI_TD_CC_SHIFT;
            int actual = ctx->maxpkt;
            if (ctx->td->cbp != 0) {
                actual = ctx->maxpkt - ((ctx->td->buffer_end - ctx->td->cbp) + 1);
            }
            if (cc == 0 || cc == 15) {
                ctx->callback(ctx->dev, ctx->buf, actual);
            }

            ctx->ed->control |= OHCI_ED_SKIP;
            asm volatile("" ::: "memory");

            uint32_t carry = ctx->ed->head_td & 0x2;

            ctx->td->control = OHCI_TD_CC_MASK | OHCI_TD_DP_IN | OHCI_TD_DI_IMMEDIATE | OHCI_TD_R;
            ctx->td->cbp = (uint32_t)(uintptr_t)ctx->buf;
            ctx->td->buffer_end = (uint32_t)(uintptr_t)ctx->buf + ctx->maxpkt - 1;
            ctx->td->next_td = ctx->ed->tail_td;
            ctx->ed->head_td = (uint32_t)(uintptr_t)ctx->td | carry;

            asm volatile("" ::: "memory");
            ctx->ed->control &= ~OHCI_ED_SKIP;
        }
    }
}

static void ohci_delay(int loops) {
    for (volatile int i = 0; i < loops; i++) {
        asm volatile("nop");
    }
}

static void ohci_reset_port(ohci_hc_t* hc, int port) {
    ohci_write(hc, OHCI_REG_RHPORTSTATUS + port, OHCI_RHPS_PRS);
    int loops = 1000000;
    while (loops--) {
        if (!(ohci_read(hc, OHCI_REG_RHPORTSTATUS + port) & OHCI_RHPS_PRS)) break;
        asm volatile("pause");
    }
    ohci_write(hc, OHCI_REG_RHPORTSTATUS + port, OHCI_RHPS_PRSC);
}

static void ohci_probe_root_ports(ohci_hc_t* hc) {
    uint32_t desc_a = ohci_read(hc, OHCI_REG_RHDESCA);
    int num_ports = desc_a & 0xFF;
    for (int port = 0; port < num_ports && port < USB_MAX_HUB_PORTS; port++) {
        uint32_t status = ohci_read(hc, OHCI_REG_RHPORTSTATUS + port);
        if (status & OHCI_RHPS_CCS) {
            ohci_reset_port(hc, port);
            ohci_delay(500000);
            status = ohci_read(hc, OHCI_REG_RHPORTSTATUS + port);
            int speed = (status & OHCI_RHPS_LSDA) ? 1 : 0;
            if (status & OHCI_RHPS_PES) {
                usb_enumerate_device(&hc->hcd, 0, port, speed);
            }
        }
    }
}

usb_hcd_t* ohci_probe_and_init(uint32_t bar0_phys) {
    if (g_hc_count >= OHCI_MAX_HC) return 0;
    ohci_hc_t* hc = &g_hc[g_hc_count++];
    memset(hc, 0, sizeof(ohci_hc_t));
    hc->regs = (volatile uint32_t*)(uintptr_t)(bar0_phys & ~0xF);

    uint32_t control = ohci_read(hc, OHCI_REG_CONTROL);
    if ((control & OHCI_CTRL_IR)) {
        ohci_write(hc, OHCI_REG_CMDSTATUS, OHCI_CMDSTATUS_OCR);
        int loops = 1000000;
        while (loops-- && (ohci_read(hc, OHCI_REG_CONTROL) & OHCI_CTRL_IR)) {
            asm volatile("pause");
        }
    }

    ohci_write(hc, OHCI_REG_CONTROL, OHCI_CTRL_HCFS_RESET);
    ohci_delay(2000000);

    hc->hcca = (ohci_hcca_t*)pmm_alloc_block();
    memset(hc->hcca, 0, sizeof(ohci_hcca_t));

    ohci_write(hc, OHCI_REG_HCCA, (uint32_t)(uintptr_t)hc->hcca);
    ohci_write(hc, OHCI_REG_CTRLHEADED, 0);
    ohci_write(hc, OHCI_REG_BULKHEADED, 0);
    ohci_write(hc, OHCI_REG_INTSTATUS, 0xFFFFFFFF);
    ohci_write(hc, OHCI_REG_INTDISABLE, 0xFFFFFFFF);

    uint32_t fminterval = ohci_read(hc, OHCI_REG_FMINTERVAL);
    uint32_t fi = fminterval & 0x3FFF;
    if (fi == 0) fi = 0x2EDF;
    ohci_write(hc, OHCI_REG_FMINTERVAL, (1 << 31) | ((((fi - 210) * 6000) / 7500) << 16) | fi);
    ohci_write(hc, OHCI_REG_PERIODICSTART, (fi * 9) / 10);

    ohci_write(hc, OHCI_REG_CONTROL, OHCI_CTRL_HCFS_OPERATIONAL | (0x3 & OHCI_CTRL_CBSR_MASK));
    ohci_delay(1000000);

    uint32_t desc_a = ohci_read(hc, OHCI_REG_RHDESCA);
    ohci_write(hc, OHCI_REG_RHSTATUS, (1 << 16));
    ohci_delay(2000000);
    (void)desc_a;

    hc->hcd.control_transfer = ohci_do_control;
    hc->hcd.setup_interrupt_in = ohci_setup_interrupt_in;
    hc->hcd.priv = hc;

    klog_status("OHCI CONTROLLER STARTED", 0x00FF00);

    ohci_probe_root_ports(hc);

    return &hc->hcd;
}