#include "uhci.h"
#include "usb.h"
#include "pci.h"
#include "pmm.h"
#include "klog.h"
#include "string.h"
#include "io.h"

#define UHCI_MAX_HC       8
#define UHCI_MAX_PORTS    8
#define UHCI_MAX_INTR     16
#define UHCI_FRAMES       1024
#define UHCI_ARENA_TDS    240

#define UHCI_REG_CMD      0x00
#define UHCI_REG_STS      0x02
#define UHCI_REG_INTR     0x04
#define UHCI_REG_FRNUM    0x06
#define UHCI_REG_FRBASE   0x08
#define UHCI_REG_SOFMOD   0x0C
#define UHCI_REG_PORTSC   0x10

#define UHCI_CMD_RS       0x0001
#define UHCI_CMD_HCRESET  0x0002
#define UHCI_CMD_CF       0x0040
#define UHCI_CMD_MAXP     0x0080

#define UHCI_STS_HCH      0x0020
#define UHCI_STS_ALL      0x003F

#define UHCI_PORT_CCS     0x0001
#define UHCI_PORT_CSC     0x0002
#define UHCI_PORT_PED     0x0004
#define UHCI_PORT_PEDC    0x0008
#define UHCI_PORT_RD      0x0040
#define UHCI_PORT_ONE     0x0080
#define UHCI_PORT_LSDA    0x0100
#define UHCI_PORT_PR      0x0200
#define UHCI_PORT_SUSP    0x1000
#define UHCI_PORT_KEEP    (UHCI_PORT_PED | UHCI_PORT_RD | UHCI_PORT_SUSP)

#define UHCI_PCI_LEGSUP   0xC0
#define UHCI_LEGSUP_RWC   0x8F00u
#define UHCI_LEGSUP_PIRQ  0x2000u

#define UHCI_LP_TERM      0x1u
#define UHCI_LP_QH        0x2u
#define UHCI_LP_DEPTH     0x4u

#define UHCI_TD_ACTIVE    (1u << 23)
#define UHCI_TD_STALLED   (1u << 22)
#define UHCI_TD_DBUFERR   (1u << 21)
#define UHCI_TD_BABBLE    (1u << 20)
#define UHCI_TD_NAK       (1u << 19)
#define UHCI_TD_CRCTO     (1u << 18)
#define UHCI_TD_BITSTUFF  (1u << 17)
#define UHCI_TD_ERRORS    (UHCI_TD_STALLED | UHCI_TD_DBUFERR | UHCI_TD_BABBLE | UHCI_TD_CRCTO | UHCI_TD_BITSTUFF)
#define UHCI_TD_CERR3     (3u << 27)
#define UHCI_TD_LS        (1u << 26)
#define UHCI_TD_SPD       (1u << 29)

#define UHCI_PID_SETUP    0x2D
#define UHCI_PID_IN       0x69
#define UHCI_PID_OUT      0xE1

#define UHCI_CONTROL_TIMEOUT_MS   500
#define UHCI_BULK_IN_TIMEOUT_MS   100
#define UHCI_BULK_OUT_TIMEOUT_MS  500

typedef struct {
    volatile uint32_t link;
    volatile uint32_t ctrl;
    volatile uint32_t token;
    volatile uint32_t buffer;
} __attribute__((aligned(16))) uhci_td_t;

typedef struct {
    volatile uint32_t head;
    volatile uint32_t element;
    uint32_t pad[2];
} __attribute__((aligned(16))) uhci_qh_t;

typedef struct {
    uhci_qh_t qh_ctrl;
    uhci_qh_t qh_bulk;
    uint8_t setup[16];
    uhci_td_t td[UHCI_ARENA_TDS];
} __attribute__((aligned(16))) uhci_arena_t;

_Static_assert(sizeof(uhci_arena_t) <= 4096, "uhci arena must fit in one page");

typedef struct {
    uint16_t io;
    int n_ports;
    uint8_t port_present[UHCI_MAX_PORTS];
    volatile uint32_t* frames;
    uhci_arena_t* arena;
    uhci_td_t* intr_tail;
    volatile int busy;
    usb_hcd_t hcd;
} uhci_hc_t;

typedef struct {
    uhci_hc_t* hc;
    usb_device_t* dev;
    void (*callback)(usb_device_t*, uint8_t*, int);
    uhci_td_t* td;
    uint8_t* buf;
    uint32_t token;
    uint32_t ctrl;
    int maxpkt;
    int toggle;
    uint32_t completions;
    uint32_t errors;
} uhci_intr_ctx_t;

static uhci_hc_t g_hc[UHCI_MAX_HC];
static int g_hc_count = 0;

static uhci_intr_ctx_t g_intr_ctx[UHCI_MAX_INTR];
static int g_intr_ctx_count = 0;
static uhci_td_t g_intr_td[UHCI_MAX_INTR];
static uint8_t g_intr_buf[UHCI_MAX_INTR][64] __attribute__((aligned(16)));

static inline void uhci_barrier(void) {
    asm volatile("" ::: "memory");
}

static inline uint32_t uhci_phys(const void* p) {
    return (uint32_t)(uintptr_t)p;
}

static void uhci_lock(uhci_hc_t* hc) {
    while (__sync_lock_test_and_set(&hc->busy, 1)) {
        asm volatile("pause");
    }
}

static void uhci_unlock(uhci_hc_t* hc) {
    __sync_lock_release(&hc->busy);
}

static uint16_t uhci_port_read(uhci_hc_t* hc, int port) {
    return inw((uint16_t)(hc->io + UHCI_REG_PORTSC + port * 2));
}

static void uhci_port_write(uhci_hc_t* hc, int port, uint16_t val) {
    outw((uint16_t)(hc->io + UHCI_REG_PORTSC + port * 2), val);
}

static uint32_t uhci_td_ctrl(usb_device_t* dev) {
    uint32_t ctrl = UHCI_TD_ACTIVE | UHCI_TD_CERR3;
    if (dev->speed == 1) ctrl |= UHCI_TD_LS;
    return ctrl;
}

static uint32_t uhci_td_token(uint32_t pid, int addr, int ep, int toggle, int len) {
    return pid | ((uint32_t)(addr & 0x7F) << 8) | ((uint32_t)(ep & 0xF) << 15) |
           ((uint32_t)(toggle & 1) << 19) | (((uint32_t)(len - 1) & 0x7FFu) << 21);
}

static int uhci_td_actual(uint32_t ctrl) {
    return (int)((ctrl + 1) & 0x7FFu);
}

static int uhci_td_expected(uint32_t token) {
    return (int)(((token >> 21) + 1) & 0x7FFu);
}

static void uhci_finish(uhci_qh_t* qh, uhci_td_t* tds, int n, int failed) {
    qh->element = UHCI_LP_TERM;
    uhci_barrier();
    if (failed) {
        for (int i = 0; i < n; i++) {
            tds[i].ctrl &= ~UHCI_TD_ACTIVE;
        }
        pit_delay_us(2000);
    }
}

static int uhci_wait_chain(uhci_qh_t* qh, uhci_td_t* tds, int n, int status_idx, uint32_t timeout_ms, int* completed, int* actual) {
    uint32_t limit = timeout_ms * 10u;
    uint32_t waited = 0;
    for (;;) {
        int pending = 0;
        int result = 0;
        int done = 0;
        int bytes = 0;
        for (int i = 0; i < n; i++) {
            uint32_t st = tds[i].ctrl;
            if (st & UHCI_TD_ACTIVE) {
                pending = 1;
                break;
            }
            if (st & UHCI_TD_ERRORS) {
                result = -1;
                break;
            }
            done++;
            int is_in = ((tds[i].token & 0xFF) == UHCI_PID_IN);
            int got = uhci_td_actual(st);
            int want = uhci_td_expected(tds[i].token);
            if (is_in && i != status_idx) bytes += got;
            if (is_in && got < want && i != status_idx) {
                if (status_idx >= 0) {
                    if (i < status_idx) {
                        if (tds[status_idx].ctrl & UHCI_TD_ACTIVE) {
                            qh->element = uhci_phys(&tds[status_idx]);
                            uhci_barrier();
                        }
                        i = status_idx - 1;
                    }
                } else {
                    break;
                }
            }
        }
        if (result < 0) {
            *completed = done;
            *actual = bytes;
            return -1;
        }
        if (!pending) {
            *completed = done;
            *actual = bytes;
            return 0;
        }
        if (++waited > limit) {
            *completed = done;
            *actual = bytes;
            return -2;
        }
        pit_delay_us(100);
    }
}

static int uhci_do_control(usb_hcd_t* hcdp, usb_device_t* dev, usb_setup_pkt_t* setup, void* buf, int len, int dir_in) {
    uhci_hc_t* hc = (uhci_hc_t*)hcdp->priv;
    if (len < 0 || (len > 0 && !buf)) return -1;

    int mps = dev->max_packet0 > 0 ? dev->max_packet0 : 8;
    int ndata = (len + mps - 1) / mps;
    int n = ndata + 2;
    if (n > UHCI_ARENA_TDS) return -1;

    uhci_lock(hc);

    uhci_arena_t* a = hc->arena;
    memcpy(a->setup, setup, sizeof(usb_setup_pkt_t));

    uint32_t ctrl = uhci_td_ctrl(dev);
    int addr = dev->address;

    a->td[0].ctrl = ctrl;
    a->td[0].token = uhci_td_token(UHCI_PID_SETUP, addr, 0, 0, (int)sizeof(usb_setup_pkt_t));
    a->td[0].buffer = uhci_phys(a->setup);

    int toggle = 1;
    int offset = 0;
    for (int i = 0; i < ndata; i++) {
        int chunk = len - offset;
        if (chunk > mps) chunk = mps;
        uhci_td_t* td = &a->td[1 + i];
        td->ctrl = dir_in ? (ctrl | UHCI_TD_SPD) : ctrl;
        td->token = uhci_td_token(dir_in ? UHCI_PID_IN : UHCI_PID_OUT, addr, 0, toggle, chunk);
        td->buffer = uhci_phys((uint8_t*)buf + offset);
        toggle ^= 1;
        offset += chunk;
    }

    int status_idx = n - 1;
    a->td[status_idx].ctrl = ctrl;
    a->td[status_idx].token = uhci_td_token(dir_in ? UHCI_PID_OUT : UHCI_PID_IN, addr, 0, 1, 0);
    a->td[status_idx].buffer = 0;

    for (int i = 0; i < n; i++) {
        a->td[i].link = (i < n - 1) ? (uhci_phys(&a->td[i + 1]) | UHCI_LP_DEPTH) : UHCI_LP_TERM;
    }
    uhci_barrier();

    a->qh_ctrl.element = uhci_phys(&a->td[0]);

    int completed = 0;
    int actual = 0;
    int r = uhci_wait_chain(&a->qh_ctrl, a->td, n, status_idx, UHCI_CONTROL_TIMEOUT_MS, &completed, &actual);

    if (r != 0) {
        klogf_color("uhci: control xfer failed r=%d dev=%d speed=%d mps=%d done=%d/%d td=0x%x\n", 0xFF0000,
            r, dev->address, dev->speed, mps, completed, n, completed < n ? a->td[completed].ctrl : 0);
    }

    uhci_finish(&a->qh_ctrl, a->td, n, r != 0);
    uhci_unlock(hc);

    if (r != 0) return -1;
    return dir_in ? actual : len;
}

static int uhci_bulk_transfer(usb_hcd_t* hcdp, usb_device_t* dev, uint8_t ep_addr, void* buf, int len, int dir_in, int* toggle) {
    uhci_hc_t* hc = (uhci_hc_t*)hcdp->priv;
    if (len <= 0 || !buf) return -1;

    int maxpkt = dir_in ? dev->ep_in_maxpkt : dev->ep_out_maxpkt;
    if (maxpkt <= 0) maxpkt = 64;

    int n = (len + maxpkt - 1) / maxpkt;
    if (n > UHCI_ARENA_TDS) return -1;

    uhci_lock(hc);

    uhci_arena_t* a = hc->arena;
    uint32_t ctrl = uhci_td_ctrl(dev);
    if (dir_in) ctrl |= UHCI_TD_SPD;

    int t = *toggle & 1;
    int offset = 0;
    for (int i = 0; i < n; i++) {
        int chunk = len - offset;
        if (chunk > maxpkt) chunk = maxpkt;
        uhci_td_t* td = &a->td[i];
        td->ctrl = ctrl;
        td->token = uhci_td_token(dir_in ? UHCI_PID_IN : UHCI_PID_OUT, dev->address, ep_addr & 0xF, t, chunk);
        td->buffer = uhci_phys((uint8_t*)buf + offset);
        td->link = (i < n - 1) ? uhci_phys(&a->td[i + 1]) : UHCI_LP_TERM;
        t ^= 1;
        offset += chunk;
    }
    uhci_barrier();

    a->qh_bulk.element = uhci_phys(&a->td[0]);

    int completed = 0;
    int actual = 0;
    int r = uhci_wait_chain(&a->qh_bulk, a->td, n, -1, dir_in ? UHCI_BULK_IN_TIMEOUT_MS : UHCI_BULK_OUT_TIMEOUT_MS, &completed, &actual);

    uhci_finish(&a->qh_bulk, a->td, n, r != 0);
    uhci_unlock(hc);

    *toggle = (*toggle ^ (completed & 1)) & 1;

    if (r != 0) return -1;
    return dir_in ? actual : len;
}

static void uhci_link_interrupt_td(uhci_hc_t* hc, uhci_td_t* td) {
    uint32_t after;
    if (hc->intr_tail) {
        after = hc->intr_tail->link;
    } else {
        after = uhci_phys(&hc->arena->qh_ctrl) | UHCI_LP_QH;
    }
    td->link = after;
    uhci_barrier();
    if (hc->intr_tail) {
        hc->intr_tail->link = uhci_phys(td);
    } else {
        for (int i = 0; i < UHCI_FRAMES; i++) {
            hc->frames[i] = uhci_phys(td);
        }
    }
    uhci_barrier();
    hc->intr_tail = td;
}

static int uhci_setup_interrupt_in(usb_hcd_t* hcdp, usb_device_t* dev, uint8_t ep_addr, uint16_t maxpkt, uint8_t interval, void (*callback)(usb_device_t*, uint8_t*, int)) {
    (void)interval;
    uhci_hc_t* hc = (uhci_hc_t*)hcdp->priv;
    if (maxpkt == 0) maxpkt = 8;
    if (maxpkt > 64) maxpkt = 64;

    int slot = -1;
    int reused = 0;
    for (int i = 0; i < g_intr_ctx_count; i++) {
        if (!g_intr_ctx[i].dev && g_intr_ctx[i].hc == hc) {
            slot = i;
            reused = 1;
            break;
        }
    }
    if (slot < 0) {
        if (g_intr_ctx_count >= UHCI_MAX_INTR) return -1;
        slot = g_intr_ctx_count;
    }

    uhci_td_t* td = &g_intr_td[slot];
    uint8_t* buf = g_intr_buf[slot];
    memset(buf, 0, 64);
    td->ctrl = 0;
    uhci_barrier();

    uhci_intr_ctx_t* ctx = &g_intr_ctx[slot];
    ctx->hc = hc;
    ctx->callback = callback;
    ctx->td = td;
    ctx->buf = buf;
    ctx->maxpkt = maxpkt;
    ctx->toggle = 0;
    ctx->completions = 0;
    ctx->errors = 0;
    ctx->ctrl = uhci_td_ctrl(dev);
    ctx->token = uhci_td_token(UHCI_PID_IN, dev->address, ep_addr & 0xF, 0, maxpkt);

    td->token = ctx->token;
    td->buffer = uhci_phys(buf);
    uhci_barrier();

    if (!reused) {
        uhci_link_interrupt_td(hc, td);
        g_intr_ctx_count++;
    }
    ctx->dev = dev;
    td->ctrl = ctx->ctrl;
    uhci_barrier();

    klogf_color("uhci: interrupt-in registered dev=%d ep=0x%x maxpkt=%d speed=%d\n", 0x00FFFF,
        dev->address, ep_addr, maxpkt, dev->speed);
    return 0;
}

static void uhci_rearm(uhci_intr_ctx_t* ctx) {
    ctx->td->token = (ctx->token & ~(1u << 19)) | ((uint32_t)(ctx->toggle & 1) << 19);
    uhci_barrier();
    ctx->td->ctrl = ctx->ctrl;
    uhci_barrier();
}

static void uhci_release_device(usb_hcd_t* hcdp, usb_device_t* dev) {
    (void)hcdp;
    for (int i = 0; i < g_intr_ctx_count; i++) {
        uhci_intr_ctx_t* ctx = &g_intr_ctx[i];
        if (ctx->dev == dev) {
            ctx->td->ctrl &= ~UHCI_TD_ACTIVE;
            ctx->dev = 0;
        }
    }
    dev->hcd_priv = 0;
}

static void uhci_probe_port(uhci_hc_t* hc, int port) {
    pit_delay_ms(100);

    uint16_t st = uhci_port_read(hc, port);
    if (!(st & UHCI_PORT_CCS)) return;

    uhci_port_write(hc, port, (uint16_t)((st & UHCI_PORT_KEEP) | UHCI_PORT_CSC | UHCI_PORT_PEDC));

    uhci_port_write(hc, port, UHCI_PORT_PR);
    pit_delay_ms(60);
    uhci_port_write(hc, port, 0);
    pit_delay_ms(20);

    int enabled = 0;
    for (int tries = 0; tries < 20; tries++) {
        st = uhci_port_read(hc, port);
        if (!(st & UHCI_PORT_CCS)) break;
        if (st & UHCI_PORT_PED) {
            enabled = 1;
            break;
        }
        uhci_port_write(hc, port, (uint16_t)((st & (UHCI_PORT_RD | UHCI_PORT_SUSP)) | UHCI_PORT_PED | UHCI_PORT_CSC | UHCI_PORT_PEDC));
        pit_delay_ms(10);
    }

    st = uhci_port_read(hc, port);
    uhci_port_write(hc, port, (uint16_t)((st & UHCI_PORT_KEEP) | UHCI_PORT_CSC | UHCI_PORT_PEDC));

    if (!enabled) {
        klogf_color("uhci: port %d did not enable after reset (portsc=0x%x)\n", 0xFF0000, port, st);
        return;
    }

    int speed = (st & UHCI_PORT_LSDA) ? 1 : 0;
    klogf_color("uhci: port %d device connected, %s speed\n", 0x00FFFF, port, speed ? "low" : "full");
    hc->port_present[port] = 1;
    pit_delay_ms(20);
    usb_enumerate_device(&hc->hcd, 0, port, speed);
}

static void uhci_remove_port_devices(uhci_hc_t* hc, int port) {
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

static void uhci_poll_ports(uhci_hc_t* hc) {
    for (int port = 0; port < hc->n_ports; port++) {
        uint16_t st = uhci_port_read(hc, port);
        if (!(st & UHCI_PORT_CSC)) continue;

        uhci_port_write(hc, port, (uint16_t)((st & UHCI_PORT_KEEP) | UHCI_PORT_CSC));

        if (st & UHCI_PORT_CCS) {
            if (hc->port_present[port]) {
                hc->port_present[port] = 0;
                uhci_remove_port_devices(hc, port);
            }
            uhci_probe_port(hc, port);
        } else if (hc->port_present[port]) {
            hc->port_present[port] = 0;
            uhci_remove_port_devices(hc, port);
            klogf_color("uhci: port %d device removed\n", 0xFFFF00, port);
        }
    }
}

void uhci_poll_interrupts(void) {
    for (int i = 0; i < g_intr_ctx_count; i++) {
        uhci_intr_ctx_t* ctx = &g_intr_ctx[i];
        if (!ctx->dev) continue;
        uint32_t st = ctx->td->ctrl;
        if (st & UHCI_TD_ACTIVE) continue;

        if (st & UHCI_TD_ERRORS) {
            ctx->errors++;
            if (ctx->errors <= 3) {
                klogf_color("uhci: intr error dev=%d slot=%d ctrl=0x%x\n", 0xFF0000, ctx->dev->address, i, st);
            }
            uhci_rearm(ctx);
            continue;
        }

        int actual = uhci_td_actual(st);
        ctx->toggle ^= 1;
        if (actual > 0) {
            ctx->completions++;
            if (ctx->completions <= 3) {
                klogf_color("uhci: intr completion dev=%d slot=%d bytes=%d b0=0x%x b1=0x%x b2=0x%x b3=0x%x\n", 0x00FF00,
                    ctx->dev->address, i, actual, ctx->buf[0], ctx->buf[1], ctx->buf[2], ctx->buf[3]);
            }
            ctx->callback(ctx->dev, ctx->buf, actual);
        }
        uhci_rearm(ctx);
    }

    for (int i = 0; i < g_hc_count; i++) {
        uhci_poll_ports(&g_hc[i]);
    }
}

usb_hcd_t* uhci_probe_and_init(uint8_t bus, uint8_t dev, uint8_t func, uint16_t io_base) {
    if (g_hc_count >= UHCI_MAX_HC) return 0;

    uhci_hc_t* hc = &g_hc[g_hc_count];
    memset(hc, 0, sizeof(uhci_hc_t));
    hc->io = io_base;

    uint32_t leg = pci_config_read(bus, dev, func, UHCI_PCI_LEGSUP);
    pci_config_write(bus, dev, func, UHCI_PCI_LEGSUP, (leg & 0xFFFF0000u) | UHCI_LEGSUP_RWC);

    outw((uint16_t)(io_base + UHCI_REG_CMD), 0);
    pit_delay_ms(5);
    outw((uint16_t)(io_base + UHCI_REG_CMD), UHCI_CMD_HCRESET);
    int reset_done = 0;
    for (int i = 0; i < 1000; i++) {
        if (!(inw((uint16_t)(io_base + UHCI_REG_CMD)) & UHCI_CMD_HCRESET)) {
            reset_done = 1;
            break;
        }
        pit_delay_ms(1);
    }
    if (!reset_done) {
        klogf_color("uhci: controller at %d:%d.%d did not leave reset\n", 0xFF0000, bus, dev, func);
        return 0;
    }
    outw((uint16_t)(io_base + UHCI_REG_INTR), 0);
    outw((uint16_t)(io_base + UHCI_REG_CMD), 0);
    outw((uint16_t)(io_base + UHCI_REG_STS), UHCI_STS_ALL);

    int ports = 0;
    for (int p = 0; p < UHCI_MAX_PORTS; p++) {
        uint16_t v = inw((uint16_t)(io_base + UHCI_REG_PORTSC + p * 2));
        if (v == 0xFFFF || !(v & UHCI_PORT_ONE)) break;
        ports++;
    }
    if (ports == 0) {
        klogf_color("uhci: controller at %d:%d.%d reports no ports\n", 0xFF0000, bus, dev, func);
        return 0;
    }
    hc->n_ports = ports;

    hc->frames = (volatile uint32_t*)pmm_alloc_block();
    hc->arena = (uhci_arena_t*)pmm_alloc_block();
    if (!hc->frames || !hc->arena) {
        klog_color("uhci: out of memory for schedule\n", 0xFF0000);
        return 0;
    }
    memset((void*)hc->frames, 0, 4096);
    memset(hc->arena, 0, 4096);

    hc->arena->qh_ctrl.head = uhci_phys(&hc->arena->qh_bulk) | UHCI_LP_QH;
    hc->arena->qh_ctrl.element = UHCI_LP_TERM;
    hc->arena->qh_bulk.head = UHCI_LP_TERM;
    hc->arena->qh_bulk.element = UHCI_LP_TERM;
    for (int i = 0; i < UHCI_FRAMES; i++) {
        hc->frames[i] = uhci_phys(&hc->arena->qh_ctrl) | UHCI_LP_QH;
    }
    uhci_barrier();

    outw((uint16_t)(io_base + UHCI_REG_FRNUM), 0);
    outl((uint16_t)(io_base + UHCI_REG_FRBASE), uhci_phys((void*)hc->frames));
    outb((uint16_t)(io_base + UHCI_REG_SOFMOD), 0x40);
    outw((uint16_t)(io_base + UHCI_REG_STS), UHCI_STS_ALL);
    outw((uint16_t)(io_base + UHCI_REG_CMD), UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP);

    int running = 0;
    for (int i = 0; i < 100; i++) {
        if (!(inw((uint16_t)(io_base + UHCI_REG_STS)) & UHCI_STS_HCH)) {
            running = 1;
            break;
        }
        pit_delay_ms(1);
    }
    if (!running) {
        klogf_color("uhci: controller at %d:%d.%d did not start\n", 0xFF0000, bus, dev, func);
        return 0;
    }

    leg = pci_config_read(bus, dev, func, UHCI_PCI_LEGSUP);
    pci_config_write(bus, dev, func, UHCI_PCI_LEGSUP, (leg & 0xFFFF0000u) | UHCI_LEGSUP_PIRQ);

    hc->hcd.control_transfer = uhci_do_control;
    hc->hcd.setup_interrupt_in = uhci_setup_interrupt_in;
    hc->hcd.bulk_transfer = uhci_bulk_transfer;
    hc->hcd.release_device = uhci_release_device;
    hc->hcd.priv = hc;

    g_hc_count++;
    klogf_color("UHCI CONTROLLER STARTED (%d ports)\n", 0x00FF00, ports);

    for (int port = 0; port < ports; port++) {
        uint16_t st = uhci_port_read(hc, port);
        if (st & UHCI_PORT_CCS) {
            uhci_probe_port(hc, port);
        }
    }

    return &hc->hcd;
}
