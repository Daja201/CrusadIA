#include "ehci.h"
#include "usb.h"
#include "pmm.h"
#include "pci.h"
#include "klog.h"
#include "string.h"
#include "paging.h"
#include "io.h"

#define EHCI_MMIO_MAP_SIZE 0x1000
#define EHCI_MMIO_UC_FLAGS 0x1B

#define EHCI_MAX_HC 4
#define EHCI_PERIODIC_ENTRIES 1024
#define EHCI_ARENA_QTDS 62
#define EHCI_QTD_MAX_BYTES 16384
#define EHCI_SPIN_LIMIT 40000000u
#define EHCI_UFRAME_MASK 0x3FFFu
#define EHCI_CONTROL_TIMEOUT_MS 1000
#define EHCI_BULK_IN_TIMEOUT_MS 100
#define EHCI_BULK_OUT_TIMEOUT_MS 500
#define EHCI_DOORBELL_TIMEOUT_MS 100
#define EHCI_ASYNC_START_TIMEOUT_MS 100
#define EHCI_NAK_RELOAD 4
#define EHCI_QTD_ERRORS (EHCI_QTD_STATUS_HALTED | EHCI_QTD_STATUS_BUFERR | EHCI_QTD_STATUS_BABBLE | EHCI_QTD_STATUS_XACTERR)
#define EHCI_STS_ALL_W1C 0x3Fu
#define EHCI_HCSPARAMS_PPC (1u << 4)

typedef struct {
    ehci_qh_t qh;
    uint8_t setup[64];
    ehci_qtd_t qtd[EHCI_ARENA_QTDS];
} ehci_arena_t;

_Static_assert(sizeof(ehci_arena_t) <= 4096, "ehci arena must fit in one page");

typedef struct {
    volatile uint8_t* base;
    volatile uint32_t* op;
    uint8_t caplength;
    int n_ports;
    int port_power_control;
    uint8_t port_present[USB_MAX_HUB_PORTS];
    ehci_qh_t* async_head;
    uint32_t* periodic_list;
    ehci_arena_t* arena;
    volatile int busy;
    usb_hcd_t hcd;
    void (*release_port_to_companion)(int port);
} ehci_hc_t;

static ehci_hc_t g_hc[EHCI_MAX_HC];
static int g_hc_count = 0;

static inline void ehci_barrier(void) {
    asm volatile("" ::: "memory");
}

static inline uint32_t ehci_op_read(ehci_hc_t* hc, int reg) {
    return *(volatile uint32_t*)(hc->base + hc->caplength + reg);
}

static inline void ehci_op_write(ehci_hc_t* hc, int reg, uint32_t val) {
    *(volatile uint32_t*)(hc->base + hc->caplength + reg) = val;
}

static inline uint32_t ehci_cap_read32(ehci_hc_t* hc, int reg) {
    return *(volatile uint32_t*)(hc->base + reg);
}

static void ehci_delay(int us) {
    pit_delay_us((uint32_t)us);
}

static void ehci_lock(ehci_hc_t* hc) {
    while (__sync_lock_test_and_set(&hc->busy, 1)) {
        asm volatile("pause");
    }
}

static void ehci_unlock(ehci_hc_t* hc) {
    __sync_lock_release(&hc->busy);
}

static uint32_t ehci_uframes_since(ehci_hc_t* hc, uint32_t start) {
    return (ehci_op_read(hc, EHCI_OP_FRINDEX) - start) & EHCI_UFRAME_MASK;
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
    for (int i = 0; i < 5; i++) {
        td->buffer[i] = 0;
    }
    if (buf && len > 0) {
        uint32_t addr = (uint32_t)(uintptr_t)buf;
        for (int i = 0; i < 5; i++) {
            td->buffer[i] = (addr & ~0xFFFu) + (uint32_t)i * 4096u;
        }
        td->buffer[0] = addr;
    }
    uint32_t token = ((uint32_t)len << 16) | ((uint32_t)(toggle & 1) << 31) | ((uint32_t)pid << 8) |
                     (3u << 10) | EHCI_QTD_STATUS_ACTIVE;
    if (ioc) token |= (1u << 15);
    ehci_barrier();
    td->token = token;
}

static void ehci_init_qh(ehci_qh_t* qh, usb_device_t* dev, int ep, int mps, int control, int periodic, ehci_qtd_t* first) {
    int hs = (dev->speed == 2);
    uint32_t eps = hs ? EHCI_QH_EPCHAR_EPS_HIGH : (dev->speed == 1 ? EHCI_QH_EPCHAR_EPS_LOW : EHCI_QH_EPCHAR_EPS_FULL);
    if (mps <= 0) mps = 8;

    uint32_t ep_char = ((uint32_t)dev->address << EHCI_QH_EPCHAR_DEVADDR_SHIFT) |
                       ((uint32_t)(ep & 0xF) << EHCI_QH_EPCHAR_EP_SHIFT) |
                       (eps << EHCI_QH_EPCHAR_EPS_SHIFT) |
                       EHCI_QH_EPCHAR_DTC |
                       ((uint32_t)(mps & 0x7FF) << EHCI_QH_EPCHAR_MPL_SHIFT);
    uint32_t ep_caps = 1u << EHCI_QH_CAPS_MULT_SHIFT;

    if (!periodic) {
        ep_char |= (uint32_t)EHCI_NAK_RELOAD << EHCI_QH_EPCHAR_NAK_RL_SHIFT;
    }
    if (!hs) {
        if (control) ep_char |= EHCI_QH_EPCHAR_CONTROL_EP;
        ep_caps |= ((uint32_t)(dev->hub_addr & 0x7F) << EHCI_QH_CAPS_HUBADDR_SHIFT) |
                   ((uint32_t)((dev->hub_port + 1) & 0x7F) << EHCI_QH_CAPS_HUBPORT_SHIFT);
    }
    if (periodic) {
        ep_caps |= 0x01u;
        if (!hs) ep_caps |= 0x1Cu << EHCI_QH_CAPS_CMASK_SHIFT;
    }

    qh->ep_char = ep_char;
    qh->ep_caps = ep_caps;
    qh->current_qtd = 0;
    qh->alt_next_qtd = EHCI_LP_TERMINATE;
    qh->token = 0;
    for (int i = 0; i < 5; i++) {
        qh->buffer[i] = 0;
    }
    ehci_barrier();
    qh->next_qtd = (uint32_t)(uintptr_t)first;
}

static int ehci_ensure_running(ehci_hc_t* hc) {
    if (!(ehci_op_read(hc, EHCI_OP_USBSTS) & EHCI_STS_HCHALTED)) return 0;
    klog_color("ehci: controller halted, restarting\n", 0xFFFF00);
    ehci_op_write(hc, EHCI_OP_USBSTS, EHCI_STS_ALL_W1C);
    ehci_op_write(hc, EHCI_OP_USBCMD, ehci_op_read(hc, EHCI_OP_USBCMD) | EHCI_CMD_RUN);
    uint32_t start = ehci_op_read(hc, EHCI_OP_FRINDEX);
    uint32_t spins = 0;
    while (ehci_op_read(hc, EHCI_OP_USBSTS) & EHCI_STS_HCHALTED) {
        if (++spins > EHCI_SPIN_LIMIT) return -1;
        if (spins > 1000 && ehci_uframes_since(hc, start) > 800) return -1;
        asm volatile("pause");
    }
    return 0;
}

static int ehci_async_enable(ehci_hc_t* hc) {
    uint32_t cmd = ehci_op_read(hc, EHCI_OP_USBCMD);
    if (!(cmd & EHCI_CMD_ASE)) {
        ehci_op_write(hc, EHCI_OP_USBCMD, cmd | EHCI_CMD_ASE);
    }
    uint32_t start = ehci_op_read(hc, EHCI_OP_FRINDEX);
    uint32_t limit = EHCI_ASYNC_START_TIMEOUT_MS * 8u;
    uint32_t spins = 0;
    while (!(ehci_op_read(hc, EHCI_OP_USBSTS) & EHCI_STS_ASS)) {
        if (++spins > EHCI_SPIN_LIMIT) break;
        if (ehci_uframes_since(hc, start) > limit) break;
        asm volatile("pause");
    }
    if (!(ehci_op_read(hc, EHCI_OP_USBSTS) & EHCI_STS_ASS)) {
        klog_color("ehci: async schedule never started (ASS never set)\n", 0xFF0000);
        return -1;
    }
    return 0;
}

static void ehci_unlink_qh(ehci_hc_t* hc, ehci_qh_t* qh) {
    hc->async_head->horiz_link = qh->horiz_link;
    ehci_barrier();

    if (ehci_op_read(hc, EHCI_OP_USBSTS) & EHCI_STS_HCHALTED) return;

    ehci_op_write(hc, EHCI_OP_USBSTS, EHCI_STS_IAA);
    ehci_op_write(hc, EHCI_OP_USBCMD, ehci_op_read(hc, EHCI_OP_USBCMD) | EHCI_CMD_IOAAD);

    uint32_t start = ehci_op_read(hc, EHCI_OP_FRINDEX);
    uint32_t limit = EHCI_DOORBELL_TIMEOUT_MS * 8u;
    uint32_t spins = 0;
    while (!(ehci_op_read(hc, EHCI_OP_USBSTS) & EHCI_STS_IAA)) {
        if (++spins > EHCI_SPIN_LIMIT) break;
        if (ehci_uframes_since(hc, start) > limit) break;
        asm volatile("pause");
    }
    if (!(ehci_op_read(hc, EHCI_OP_USBSTS) & EHCI_STS_IAA)) {
        klog_color("ehci: async advance doorbell timed out\n", 0xFF0000);
    }
    ehci_op_write(hc, EHCI_OP_USBSTS, EHCI_STS_IAA);
}

static int ehci_wait_done(ehci_hc_t* hc, ehci_qh_t* qh, ehci_qtd_t* last, uint32_t timeout_ms) {
    uint32_t start = ehci_op_read(hc, EHCI_OP_FRINDEX);
    uint32_t limit = timeout_ms * 8u;
    uint32_t spins = 0;
    for (;;) {
        uint32_t tok = last->token;
        if (!(tok & EHCI_QTD_STATUS_ACTIVE)) {
            return (tok & EHCI_QTD_ERRORS) ? -1 : 0;
        }
        if (qh->token & EHCI_QTD_STATUS_HALTED) return -1;
        if (++spins > EHCI_SPIN_LIMIT) return -2;
        if (ehci_uframes_since(hc, start) > limit) return -2;
        asm volatile("pause");
    }
}

static int ehci_submit(ehci_hc_t* hc, ehci_qh_t* qh, ehci_qtd_t* last, uint32_t timeout_ms) {
    if (ehci_ensure_running(hc) < 0) return -3;

    ehci_op_write(hc, EHCI_OP_USBSTS, EHCI_STS_USBINT | EHCI_STS_USBERRINT);

    qh->horiz_link = hc->async_head->horiz_link;
    ehci_barrier();
    hc->async_head->horiz_link = (uint32_t)(uintptr_t)qh | EHCI_LP_TYPE_QH;
    ehci_barrier();

    if (ehci_async_enable(hc) < 0) return -3;

    return ehci_wait_done(hc, qh, last, timeout_ms);
}

static void ehci_report_failure(ehci_hc_t* hc, usb_device_t* dev, int kind, int result, ehci_qh_t* qh, ehci_qtd_t* tds, int count) {
    klogf_color("ehci: %s xfer failed r=%d dev=%d speed=%d mps=%d hub=%d/%d\n", 0xFF0000,
        kind == 0 ? "control" : "bulk", result, dev->address, dev->speed, dev->max_packet0, dev->hub_addr, dev->hub_port);
    klogf_color("ehci: qh char=0x%x caps=0x%x cur=0x%x next=0x%x token=0x%x\n", 0xFF0000,
        qh->ep_char, qh->ep_caps, qh->current_qtd, qh->next_qtd, qh->token);
    for (int i = 0; i < count; i++) {
        klogf_color("ehci: qtd%d token=0x%x next=0x%x\n", 0xFF0000, i, tds[i].token, tds[i].next_qtd);
    }
    klogf_color("ehci: usbsts=0x%x usbcmd=0x%x frindex=0x%x\n", 0xFF0000,
        ehci_op_read(hc, EHCI_OP_USBSTS), ehci_op_read(hc, EHCI_OP_USBCMD),
        ehci_op_read(hc, EHCI_OP_FRINDEX));
}

static int ehci_do_control(usb_hcd_t* hcdp, usb_device_t* dev, usb_setup_pkt_t* setup, void* buf, int len, int dir_in) {
    ehci_hc_t* hc = (ehci_hc_t*)hcdp->priv;
    if (len < 0 || (len > 0 && !buf)) return -1;

    int mps = dev->max_packet0 > 0 ? dev->max_packet0 : 8;
    if (dev->speed == 2 && dev->address == 0) mps = 64;

    int nchunks = (len + EHCI_QTD_MAX_BYTES - 1) / EHCI_QTD_MAX_BYTES;
    if (nchunks + 2 > EHCI_ARENA_QTDS) return -1;

    ehci_lock(hc);

    ehci_arena_t* a = hc->arena;
    memset(a, 0, sizeof(*a));
    memcpy(a->setup, setup, sizeof(usb_setup_pkt_t));

    ehci_qtd_t* setup_td = &a->qtd[0];
    ehci_qtd_t* status_td = &a->qtd[1 + nchunks];

    ehci_fill_qtd(setup_td, EHCI_QTD_PID_SETUP, 0, a->setup, sizeof(usb_setup_pkt_t), 0);

    ehci_qtd_t* prev = setup_td;
    int toggle = 1;
    int offset = 0;
    for (int c = 0; c < nchunks; c++) {
        int chunk = len - offset;
        if (chunk > EHCI_QTD_MAX_BYTES) chunk = EHCI_QTD_MAX_BYTES;
        ehci_qtd_t* td = &a->qtd[1 + c];
        ehci_fill_qtd(td, dir_in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT, toggle, (uint8_t*)buf + offset, chunk, 0);
        td->alt_next_qtd = (uint32_t)(uintptr_t)status_td;
        prev->next_qtd = (uint32_t)(uintptr_t)td;
        prev = td;
        toggle ^= ((chunk + mps - 1) / mps) & 1;
        offset += chunk;
    }

    ehci_fill_qtd(status_td, dir_in ? EHCI_QTD_PID_OUT : EHCI_QTD_PID_IN, 1, 0, 0, 1);
    prev->next_qtd = (uint32_t)(uintptr_t)status_td;

    ehci_qh_t* qh = &a->qh;
    memset(qh, 0, sizeof(*qh));
    ehci_init_qh(qh, dev, 0, mps, 1, 0, setup_td);

    int r = ehci_submit(hc, qh, status_td, EHCI_CONTROL_TIMEOUT_MS);

    if (r != 0) {
        ehci_report_failure(hc, dev, 0, r, qh, a->qtd, nchunks + 2);
    }

    int actual = len;
    if (r == 0) {
        for (int c = 0; c < nchunks; c++) {
            int chunk = len - c * EHCI_QTD_MAX_BYTES;
            if (chunk > EHCI_QTD_MAX_BYTES) chunk = EHCI_QTD_MAX_BYTES;
            uint32_t tok = a->qtd[1 + c].token;
            if (tok & EHCI_QTD_STATUS_ACTIVE) {
                actual -= chunk;
            } else {
                actual -= (int)((tok >> 16) & 0x7FFF);
            }
        }
        if (actual < 0) actual = 0;
    }

    ehci_unlink_qh(hc, qh);
    ehci_unlock(hc);

    if (r != 0) return -1;
    return actual;
}

static int ehci_bulk_transfer(usb_hcd_t* hcdp, usb_device_t* dev, uint8_t ep_addr, void* buf, int len, int dir_in, int* toggle) {
    ehci_hc_t* hc = (ehci_hc_t*)hcdp->priv;
    if (len <= 0 || len > EHCI_QTD_MAX_BYTES || !buf) return -1;

    int maxpkt = dir_in ? dev->ep_in_maxpkt : dev->ep_out_maxpkt;
    if (maxpkt <= 0) maxpkt = 64;

    ehci_lock(hc);

    ehci_arena_t* a = hc->arena;
    memset(a, 0, sizeof(*a));

    ehci_qtd_t* td = &a->qtd[0];
    ehci_fill_qtd(td, dir_in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT, *toggle & 1, buf, len, 1);

    ehci_qh_t* qh = &a->qh;
    ehci_init_qh(qh, dev, ep_addr & 0xF, maxpkt, 0, 0, td);

    int r = ehci_submit(hc, qh, td, dir_in ? EHCI_BULK_IN_TIMEOUT_MS : EHCI_BULK_OUT_TIMEOUT_MS);

    uint32_t tok = td->token;
    int remaining = (tok >> 16) & 0x7FFF;
    int actual = len - remaining;

    if (r != 0 && r != -2) {
        ehci_report_failure(hc, dev, 1, r, qh, td, 1);
    }

    ehci_unlink_qh(hc, qh);
    ehci_unlock(hc);

    if (r != 0) return -1;

    int packets = (actual > 0) ? ((actual + maxpkt - 1) / maxpkt) : 1;
    *toggle = (*toggle ^ (packets & 1)) & 1;
    return actual;
}

typedef struct {
    usb_device_t* dev;
    void (*callback)(usb_device_t*, uint8_t*, int);
    uint8_t* buf;
    int maxpkt;
    ehci_qh_t* qh;
    ehci_qtd_t* td;
    int toggle;
    uint32_t completions;
    uint32_t errors;
} ehci_intr_ctx_t;

static ehci_intr_ctx_t g_intr_ctx[16];
static int g_intr_ctx_count = 0;
static uint8_t g_intr_bufs[16][64];

static int ehci_setup_interrupt_in(usb_hcd_t* hcdp, usb_device_t* dev, uint8_t ep_addr, uint16_t maxpkt, uint8_t interval, void (*callback)(usb_device_t*, uint8_t*, int)) {
    (void)interval;
    ehci_hc_t* hc = (ehci_hc_t*)hcdp->priv;
    if (g_intr_ctx_count >= 16) return -1;
    if (maxpkt > 64) maxpkt = 64;

    uint8_t* buf = g_intr_bufs[g_intr_ctx_count];
    ehci_qtd_t* td = ehci_alloc_qtd();
    ehci_fill_qtd(td, EHCI_QTD_PID_IN, 0, buf, maxpkt, 1);

    ehci_qh_t* qh = ehci_alloc_qh();
    ehci_init_qh(qh, dev, ep_addr & 0xF, maxpkt, 0, 1, td);

    uint32_t prev_head = hc->periodic_list[0];
    qh->horiz_link = prev_head;
    ehci_barrier();
    for (int i = 0; i < EHCI_PERIODIC_ENTRIES; i++) {
        hc->periodic_list[i] = (uint32_t)(uintptr_t)qh | EHCI_LP_TYPE_QH;
    }
    ehci_barrier();

    klogf_color("ehci: interrupt-in registered dev=%d ep=0x%x maxpkt=%d chained_prev=0x%x\n", 0x00FFFF,
        dev->address, ep_addr, maxpkt, prev_head);

    ehci_op_write(hc, EHCI_OP_USBCMD, ehci_op_read(hc, EHCI_OP_USBCMD) | EHCI_CMD_PSE);

    ehci_intr_ctx_t* ctx = &g_intr_ctx[g_intr_ctx_count++];
    ctx->dev = dev;
    ctx->callback = callback;
    ctx->buf = buf;
    ctx->maxpkt = maxpkt;
    ctx->qh = qh;
    ctx->td = td;
    ctx->toggle = 0;
    ctx->completions = 0;
    ctx->errors = 0;
    return 0;
}

static void ehci_reset_and_route_port(ehci_hc_t* hc, int port);

static void ehci_remove_port_devices(ehci_hc_t* hc, int port) {
    int index = 0;
    for (;;) {
        usb_device_t* d = usb_get_device(index);
        if (!d) break;
        if (d->hcd == &hc->hcd && d->hub_addr == 0 && d->hub_port == port) {
            usb_free_device(d);
        } else {
            index++;
        }
    }
}

static void ehci_poll_ports(ehci_hc_t* hc) {
    for (int port = 0; port < hc->n_ports; port++) {
        int reg = EHCI_OP_PORTSC + port * 4;
        uint32_t status = ehci_op_read(hc, reg);
        if (!(status & EHCI_PORTSC_CSC)) continue;

        uint32_t clear = (status & ~(EHCI_PORTSC_CSC | EHCI_PORTSC_PEDC | EHCI_PORTSC_OCC)) | EHCI_PORTSC_CSC;
        ehci_op_write(hc, reg, clear);

        if (hc->port_present[port]) {
            hc->port_present[port] = 0;
            ehci_remove_port_devices(hc, port);
            klogf_color("ehci: port %d device removed\n", 0xFFFF00, port);
        }

        if (status & EHCI_PORTSC_CCS) {
            ehci_delay(100000);
            klogf_color("ehci: port %d connect detected\n", 0x00FFFF, port);
            ehci_reset_and_route_port(hc, port);
        }
    }
}

void ehci_poll_interrupts(void) {
    for (int h = 0; h < g_hc_count; h++) {
        ehci_poll_ports(&g_hc[h]);
    }
    for (int i = 0; i < g_intr_ctx_count; i++) {
        ehci_intr_ctx_t* ctx = &g_intr_ctx[i];
        uint32_t tok = ctx->td->token;
        if (!(tok & EHCI_QTD_STATUS_ACTIVE)) {
            int cc_ok = !(tok & EHCI_QTD_ERRORS);
            int remaining = (tok >> 16) & 0x7FFF;
            int actual = ctx->maxpkt - remaining;
            if (cc_ok && actual > 0) {
                ctx->completions++;
                if (ctx->completions <= 3) {
                    klogf_color("ehci: intr completion dev=%d slot=%d bytes=%d b0=0x%x b1=0x%x b2=0x%x b3=0x%x\n", 0x00FF00,
                        ctx->dev->address, i, actual, ctx->buf[0], ctx->buf[1], ctx->buf[2], ctx->buf[3]);
                }
                ctx->callback(ctx->dev, ctx->buf, actual);
            } else if (!cc_ok) {
                ctx->errors++;
                if (ctx->errors <= 3) {
                    klogf_color("ehci: intr error dev=%d slot=%d token=0x%x\n", 0xFF0000, ctx->dev->address, i, tok);
                }
            }
            if (cc_ok) {
                ctx->toggle ^= 1;
            }
            ehci_fill_qtd(ctx->td, EHCI_QTD_PID_IN, ctx->toggle, ctx->buf, ctx->maxpkt, 1);
            ctx->qh->token = 0;
            ehci_barrier();
            ctx->qh->next_qtd = (uint32_t)(uintptr_t)ctx->td;
        }
    }
}

static void ehci_bios_handoff(uint8_t bus, uint8_t dev, uint8_t func, uint32_t hccparams) {
    uint32_t eecp = (hccparams >> 8) & 0xFF;
    int guard = 0;
    while (eecp >= 0x40 && guard++ < 16) {
        uint32_t cap = pci_config_read(bus, dev, func, eecp);
        if ((cap & 0xFF) == 0x01) {
            uint32_t legsup = cap;
            uint32_t legctlsts = pci_config_read(bus, dev, func, eecp + 4);
            klogf_color("ehci: legsup=0x%x legctlsts=0x%x (bios_owned=%d)\n", 0x00FFFF,
                legsup, legctlsts, (legsup >> 16) & 1);

            if (legsup & (1u << 16)) {
                pci_config_write(bus, dev, func, eecp, legsup | (1u << 24));
                int loops = 1000;
                while (loops--) {
                    uint32_t v = pci_config_read(bus, dev, func, eecp);
                    if ((v & (1u << 24)) && !(v & (1u << 16))) break;
                    ehci_delay(1000);
                }
                legsup = pci_config_read(bus, dev, func, eecp);
                if (legsup & (1u << 16)) {
                    klog_color("ehci: BIOS did not release ownership in time\n", 0xFF0000);
                } else {
                    klog_color("ehci: BIOS ownership released\n", 0x00FF00);
                }
            }

            pci_config_write(bus, dev, func, eecp + 4, 0);
            legctlsts = pci_config_read(bus, dev, func, eecp + 4);
            klogf_color("ehci: legctlsts after SMI disable=0x%x\n", 0x00FFFF, legctlsts);
            return;
        }
        eecp = (cap >> 8) & 0xFF;
    }
}

static uint32_t ehci_portsc_safe(uint32_t status) {
    return status & ~(EHCI_PORTSC_CSC | EHCI_PORTSC_PEDC | EHCI_PORTSC_OCC);
}

static void ehci_hand_to_companion(ehci_hc_t* hc, int port) {
    uint32_t status = ehci_portsc_safe(ehci_op_read(hc, EHCI_OP_PORTSC + port * 4));
    ehci_op_write(hc, EHCI_OP_PORTSC + port * 4, status | EHCI_PORTSC_PO);
    klogf_color("ehci: port %d handed to companion controller\n", 0x00FFFF, port);
    if (hc->release_port_to_companion) {
        hc->release_port_to_companion(port);
    }
}

static void ehci_reset_and_route_port(ehci_hc_t* hc, int port) {
    int reg = EHCI_OP_PORTSC + port * 4;
    uint32_t status = ehci_op_read(hc, reg);
    if (!(status & EHCI_PORTSC_CCS)) return;

    if ((status & EHCI_PORTSC_LINESTATUS_MASK) == (1u << 10)) {
        ehci_hand_to_companion(hc, port);
        return;
    }

    status = ehci_portsc_safe(status);
    status &= ~EHCI_PORTSC_PED;
    status |= EHCI_PORTSC_PR;
    ehci_op_write(hc, reg, status);
    klogf_color("ehci: port %d asserting reset\n", 0x00FFFF, port);
    ehci_delay(60000);

    status = ehci_portsc_safe(ehci_op_read(hc, reg));
    status &= ~EHCI_PORTSC_PR;
    ehci_op_write(hc, reg, status);

    for (int i = 0; i < 20; i++) {
        if (!(ehci_op_read(hc, reg) & EHCI_PORTSC_PR)) break;
        ehci_delay(1000);
    }
    ehci_delay(50000);

    status = ehci_op_read(hc, reg);
    klogf_color("ehci: port %d portsc after reset=0x%x (ccs=%d ped=%d)\n", 0x00FFFF,
        port, status, status & EHCI_PORTSC_CCS ? 1 : 0, status & EHCI_PORTSC_PED ? 1 : 0);

    if (!(status & EHCI_PORTSC_CCS)) return;

    if (status & EHCI_PORTSC_PED) {
        hc->port_present[port] = 1;
        usb_enumerate_device(&hc->hcd, 0, port, 2);
    } else {
        ehci_hand_to_companion(hc, port);
    }
}

usb_hcd_t* ehci_probe_and_init(uint32_t bar0_phys, void (*release_port_to_companion)(int port)) {
    if (g_hc_count >= EHCI_MAX_HC) return 0;
    ehci_hc_t* hc = &g_hc[g_hc_count++];
    memset(hc, 0, sizeof(ehci_hc_t));
    uint32_t mmio_base = bar0_phys & ~0xFu;
    for (uint32_t off = 0; off < EHCI_MMIO_MAP_SIZE; off += 4096) {
        map_page(mmio_base + off, mmio_base + off, EHCI_MMIO_UC_FLAGS);
    }
    hc->base = (volatile uint8_t*)(uintptr_t)(bar0_phys & ~0xF);
    hc->caplength = *(volatile uint8_t*)(hc->base + EHCI_CAP_CAPLENGTH);
    hc->release_port_to_companion = release_port_to_companion;

    uint32_t hcsparams = ehci_cap_read32(hc, EHCI_CAP_HCSPARAMS);
    hc->n_ports = hcsparams & 0xF;
    hc->port_power_control = (hcsparams & EHCI_HCSPARAMS_PPC) ? 1 : 0;
    if (hc->n_ports > USB_MAX_HUB_PORTS) hc->n_ports = USB_MAX_HUB_PORTS;

    ehci_op_write(hc, EHCI_OP_USBCMD, ehci_op_read(hc, EHCI_OP_USBCMD) & ~(EHCI_CMD_RUN | EHCI_CMD_ASE | EHCI_CMD_PSE));
    for (int i = 0; i < 100; i++) {
        if (ehci_op_read(hc, EHCI_OP_USBSTS) & EHCI_STS_HCHALTED) break;
        ehci_delay(1000);
    }

    ehci_op_write(hc, EHCI_OP_USBCMD, EHCI_CMD_HCRESET);
    for (int i = 0; i < 250; i++) {
        if (!(ehci_op_read(hc, EHCI_OP_USBCMD) & EHCI_CMD_HCRESET)) break;
        ehci_delay(1000);
    }
    ehci_delay(2000);

    hc->periodic_list = (uint32_t*)pmm_alloc_block();
    for (int i = 0; i < EHCI_PERIODIC_ENTRIES; i++) {
        hc->periodic_list[i] = EHCI_LP_TERMINATE;
    }

    hc->arena = (ehci_arena_t*)pmm_alloc_block();
    memset(hc->arena, 0, 4096);

    hc->async_head = ehci_alloc_qh();
    hc->async_head->horiz_link = (uint32_t)(uintptr_t)hc->async_head | EHCI_LP_TYPE_QH;
    hc->async_head->ep_char = EHCI_QH_EPCHAR_H;
    hc->async_head->ep_caps = 0;
    hc->async_head->current_qtd = 0;
    hc->async_head->next_qtd = EHCI_LP_TERMINATE;
    hc->async_head->alt_next_qtd = EHCI_LP_TERMINATE;
    hc->async_head->token = EHCI_QTD_STATUS_HALTED;
    ehci_barrier();

    ehci_op_write(hc, EHCI_OP_USBSTS, EHCI_STS_ALL_W1C);
    ehci_op_write(hc, EHCI_OP_FRINDEX, 0);
    ehci_op_write(hc, EHCI_OP_CTRLDSSEGMENT, 0);
    ehci_op_write(hc, EHCI_OP_PERIODICLISTBASE, (uint32_t)(uintptr_t)hc->periodic_list);
    ehci_op_write(hc, EHCI_OP_ASYNCLISTADDR, (uint32_t)(uintptr_t)hc->async_head);
    ehci_op_write(hc, EHCI_OP_USBINTR, 0);

    ehci_op_write(hc, EHCI_OP_USBCMD, EHCI_CMD_RUN | EHCI_CMD_ASE | EHCI_CMD_ITC_1);
    for (int i = 0; i < 100; i++) {
        if (!(ehci_op_read(hc, EHCI_OP_USBSTS) & EHCI_STS_HCHALTED)) break;
        ehci_delay(1000);
    }
    ehci_op_write(hc, EHCI_OP_CONFIGFLAG, 1);
    ehci_delay(5000);

    hc->hcd.control_transfer = ehci_do_control;
    hc->hcd.setup_interrupt_in = ehci_setup_interrupt_in;
    hc->hcd.bulk_transfer = ehci_bulk_transfer;
    hc->hcd.priv = hc;

    klog_status("EHCI CONTROLLER STARTED", 0x00FF00);

    for (int port = 0; port < hc->n_ports; port++) {
        uint32_t status = ehci_op_read(hc, EHCI_OP_PORTSC + port * 4);
        if (!(status & EHCI_PORTSC_PP)) {
            ehci_op_write(hc, EHCI_OP_PORTSC + port * 4, ehci_portsc_safe(status) | EHCI_PORTSC_PP);
        }
    }
    ehci_delay(150000);

    for (int port = 0; port < hc->n_ports; port++) {
        ehci_reset_and_route_port(hc, port);
    }

    return &hc->hcd;
}

void ehci_bios_handoff_pci(uint8_t bus, uint8_t dev, uint8_t func, uint32_t bar0_phys) {
    uint32_t mmio_base = bar0_phys & ~0xFu;
    for (uint32_t off = 0; off < EHCI_MMIO_MAP_SIZE; off += 4096) {
        map_page(mmio_base + off, mmio_base + off, EHCI_MMIO_UC_FLAGS);
    }
    volatile uint8_t* base = (volatile uint8_t*)(uintptr_t)(bar0_phys & ~0xF);
    uint32_t hccparams = *(volatile uint32_t*)(base + EHCI_CAP_HCCPARAMS);
    ehci_bios_handoff(bus, dev, func, hccparams);
}
