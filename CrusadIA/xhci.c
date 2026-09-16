#include "xhci.h"
#include "usb.h"
#include "pmm.h"
#include "pci.h"
#include "klog.h"
#include "string.h"

#define XHCI_MAX_HC        4
#define XHCI_MAX_SLOTS     32
#define XHCI_CMD_TRB_COUNT  256
#define XHCI_EVT_TRB_COUNT  256
#define XHCI_XFER_TRB_COUNT 256
#define XHCI_TIMEOUT_LOOPS 4000000
#define XHCI_MAX_DEVCTX    32
#define XHCI_MAX_INTR      16

#define XHCI_EP_TYPE_BULK_OUT      2
#define XHCI_EP_TYPE_CONTROL       4
#define XHCI_EP_TYPE_BULK_IN       6
#define XHCI_EP_TYPE_INTERRUPT_IN  7

typedef struct {
    int slot_id;
    void* input_ctx;
    void* output_ctx;
    xhci_trb_t* ep0_ring;
    int ep0_enq;
    int ep0_cycle;
    xhci_trb_t* bulk_in_ring;
    int bulk_in_enq;
    int bulk_in_cycle;
    int bulk_in_configured;
    xhci_trb_t* bulk_out_ring;
    int bulk_out_enq;
    int bulk_out_cycle;
    int bulk_out_configured;
} xhci_dev_ctx_t;

typedef struct {
    volatile uint8_t* base;
    volatile uint8_t* op;
    volatile uint8_t* rt;
    volatile uint32_t* db;
    uint32_t* dcbaa;
    xhci_trb_t* cmd_ring;
    int cmd_enq;
    int cmd_cycle;
    xhci_trb_t* evt_ring;
    int evt_deq;
    int evt_cycle;
    void* erst;
    int ctx_size;
    int n_ports;
    int max_slots;
    int pending_port;
    int pending_speed_psi;
    usb_hcd_t hcd;
} xhci_hc_t;

static xhci_hc_t g_hc[XHCI_MAX_HC];
static int g_hc_count = 0;

static xhci_dev_ctx_t g_devctx[XHCI_MAX_DEVCTX];
static int g_devctx_used = 0;

typedef struct {
    int active;
    xhci_hc_t* hc;
    usb_device_t* dev;
    void (*callback)(usb_device_t*, uint8_t*, int);
    xhci_trb_t* ring;
    int enq;
    int cycle;
    uint8_t* buf;
    int maxpkt;
    int dci;
    int slot_id;
    xhci_trb_t* outstanding;
} xhci_intr_ctx_t;

static xhci_intr_ctx_t g_intr[XHCI_MAX_INTR];
static int g_intr_count = 0;
static uint8_t g_intr_bufs[XHCI_MAX_INTR][64];

static inline uint32_t op_r(xhci_hc_t* hc, int reg) { return *(volatile uint32_t*)(hc->op + reg); }
static inline void op_w(xhci_hc_t* hc, int reg, uint32_t v) { *(volatile uint32_t*)(hc->op + reg) = v; }
static inline uint32_t rt_r(xhci_hc_t* hc, int reg) { return *(volatile uint32_t*)(hc->rt + reg); }
static inline void rt_w(xhci_hc_t* hc, int reg, uint32_t v) { *(volatile uint32_t*)(hc->rt + reg) = v; }
static inline uint32_t cap_r(xhci_hc_t* hc, int reg) { return *(volatile uint32_t*)(hc->base + reg); }

static void xhci_delay(int loops) {
    for (volatile int i = 0; i < loops; i++) asm volatile("nop");
}

static xhci_trb_t* xhci_ring_push(xhci_trb_t* ring, int count, int* enq, int* cycle,
                                   uint32_t p_lo, uint32_t p_hi, uint32_t status, uint32_t control_no_cycle) {
    xhci_trb_t* trb = &ring[*enq];
    trb->p_lo = p_lo;
    trb->p_hi = p_hi;
    trb->status = status;
    trb->control = control_no_cycle | (uint32_t)(*cycle & 1);
    (*enq)++;
    if (*enq >= count - 1) {
        xhci_trb_t* link = &ring[count - 1];
        link->p_lo = (uint32_t)(uintptr_t)ring;
        link->p_hi = 0;
        link->status = 0;
        link->control = ((uint32_t)XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_TC | (uint32_t)(*cycle & 1);
        *enq = 0;
        *cycle ^= 1;
    }
    return trb;
}

static void xhci_update_erdp(xhci_hc_t* hc) {
    uint32_t addr = (uint32_t)(uintptr_t)&hc->evt_ring[hc->evt_deq];
    rt_w(hc, XHCI_RT_IR0 + XHCI_IR_ERDP_LO, addr | (1u << 3));
    rt_w(hc, XHCI_RT_IR0 + XHCI_IR_ERDP_HI, 0);
}

static int xhci_process_one_event(xhci_hc_t* hc, xhci_trb_t* out) {
    xhci_trb_t* ev = &hc->evt_ring[hc->evt_deq];
    uint32_t control = ev->control;
    if ((control & 1) != (uint32_t)(hc->evt_cycle & 1)) return 0;

    if (out) *out = *ev;

    int type = (int)((control >> XHCI_TRB_TYPE_SHIFT) & 0x3F);
    if (type == XHCI_TRB_TYPE_TRANSFER_EVENT) {
        for (int i = 0; i < g_intr_count; i++) {
            xhci_intr_ctx_t* ic = &g_intr[i];
            if (ic->active && ic->outstanding == (xhci_trb_t*)(uintptr_t)ev->p_lo) {
                int comp_ok = (((ev->status >> 24) & 0xFF) == XHCI_COMP_SUCCESS);
                int remaining = ev->status & 0xFFFFFF;
                int actual = ic->maxpkt - remaining;
                if (comp_ok && actual > 0) {
                    ic->callback(ic->dev, ic->buf, actual);
                }
                xhci_trb_t* trb = xhci_ring_push(ic->ring, XHCI_XFER_TRB_COUNT, &ic->enq, &ic->cycle,
                    (uint32_t)(uintptr_t)ic->buf, 0, ic->maxpkt,
                    ((uint32_t)XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC);
                ic->outstanding = trb;
                ic->hc->db[ic->slot_id] = (uint32_t)ic->dci;
                break;
            }
        }
    }

    hc->evt_deq++;
    if (hc->evt_deq >= XHCI_EVT_TRB_COUNT) { hc->evt_deq = 0; hc->evt_cycle ^= 1; }
    xhci_update_erdp(hc);
    return 1;
}

void xhci_poll_interrupts(void) {
    for (int h = 0; h < g_hc_count; h++) {
        xhci_hc_t* hc = &g_hc[h];
        for (int i = 0; i < 32; i++) {
            if (!xhci_process_one_event(hc, 0)) break;
        }
    }
}

static int xhci_wait_for_trb(xhci_hc_t* hc, void* trb_ptr, uint32_t* out_slot_id) {
    int loops = XHCI_TIMEOUT_LOOPS;
    xhci_trb_t ev;
    while (loops--) {
        if (xhci_process_one_event(hc, &ev)) {
            if (ev.p_lo == (uint32_t)(uintptr_t)trb_ptr) {
                int comp = (ev.status >> 24) & 0xFF;
                if (out_slot_id) *out_slot_id = (ev.control >> 24) & 0xFF;
                return (comp == XHCI_COMP_SUCCESS) ? 0 : -1;
            }
            continue;
        }
        asm volatile("pause");
    }
    return -2;
}

static int xhci_run_command(xhci_hc_t* hc, uint32_t p_lo, uint32_t p_hi, uint32_t status, uint32_t control_no_cycle, uint32_t* out_slot_id) {
    xhci_trb_t* trb = xhci_ring_push(hc->cmd_ring, XHCI_CMD_TRB_COUNT, &hc->cmd_enq, &hc->cmd_cycle, p_lo, p_hi, status, control_no_cycle);
    hc->db[0] = 0;
    return xhci_wait_for_trb(hc, trb, out_slot_id);
}

static xhci_dev_ctx_t* xhci_alloc_devctx(void) {
    if (g_devctx_used >= XHCI_MAX_DEVCTX) return 0;
    xhci_dev_ctx_t* dc = &g_devctx[g_devctx_used++];
    memset(dc, 0, sizeof(*dc));
    return dc;
}

static int xhci_default_mps_for_speed(int speed_psi) {
    if (speed_psi == 4) return 512;
    if (speed_psi == 3) return 64;
    return 8;
}

static int xhci_speed_psi_to_generic(int psi) {
    if (psi == 2) return 1;
    if (psi == 3 || psi == 4) return 2;
    return 0;
}

static int xhci_calc_interval(int speed_psi, uint8_t bInterval) {
    if (speed_psi == 3 || speed_psi == 4) {
        int v = bInterval - 1;
        if (v < 0) v = 0;
        if (v > 15) v = 15;
        return v;
    }
    int exp = 0;
    while ((1 << exp) < bInterval && exp < 10) exp++;
    int r = exp + 3;
    if (r > 10) r = 10;
    if (r < 3) r = 3;
    return r;
}

static xhci_dev_ctx_t* xhci_ensure_slot(xhci_hc_t* hc, usb_device_t* dev) {
    if (dev->hcd_priv) return (xhci_dev_ctx_t*)dev->hcd_priv;

    xhci_dev_ctx_t* dc = xhci_alloc_devctx();
    if (!dc) return 0;

    uint32_t slot_id = 0;
    if (xhci_run_command(hc, 0, 0, 0, (uint32_t)XHCI_TRB_TYPE_ENABLE_SLOT << XHCI_TRB_TYPE_SHIFT, &slot_id) != 0) return 0;
    dc->slot_id = (int)slot_id;

    void* outctx = pmm_alloc_block();
    if (!outctx) return 0;
    memset(outctx, 0, 4096);
    hc->dcbaa[dc->slot_id * 2] = (uint32_t)(uintptr_t)outctx;
    hc->dcbaa[dc->slot_id * 2 + 1] = 0;
    dc->output_ctx = outctx;

    void* inctx = pmm_alloc_block();
    if (!inctx) return 0;
    memset(inctx, 0, 4096);
    dc->input_ctx = inctx;

    dc->ep0_ring = (xhci_trb_t*)pmm_alloc_block();
    if (!dc->ep0_ring) return 0;
    memset(dc->ep0_ring, 0, 4096);
    dc->ep0_enq = 0;
    dc->ep0_cycle = 1;

    uint32_t* icc = (uint32_t*)inctx;
    icc[1] = 0x3;

    uint32_t* slot_ctx = (uint32_t*)((uint8_t*)inctx + hc->ctx_size);
    int speed_psi = hc->pending_speed_psi;
    slot_ctx[0] = (1u << 27) | ((uint32_t)speed_psi << 20);
    slot_ctx[1] = (uint32_t)hc->pending_port << 16;

    uint32_t* ep0_ctx = (uint32_t*)((uint8_t*)inctx + 2 * hc->ctx_size);
    int mps = xhci_default_mps_for_speed(speed_psi);
    ep0_ctx[1] = ((uint32_t)XHCI_EP_TYPE_CONTROL << 3) | (3u << 1) | ((uint32_t)mps << 16);
    ep0_ctx[2] = ((uint32_t)(uintptr_t)dc->ep0_ring) | 1u;
    ep0_ctx[3] = 0;
    ep0_ctx[4] = 8;

    if (xhci_run_command(hc, (uint32_t)(uintptr_t)inctx, 0, 0,
            ((uint32_t)XHCI_TRB_TYPE_ADDRESS_DEVICE << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_BSR | ((uint32_t)dc->slot_id << 24), 0) != 0) {
        return 0;
    }

    dev->hcd_priv = dc;
    return dc;
}

static int xhci_address_device_real(xhci_hc_t* hc, xhci_dev_ctx_t* dc) {
    uint32_t* icc = (uint32_t*)dc->input_ctx;
    icc[0] = 0;
    icc[1] = 0x3;
    return xhci_run_command(hc, (uint32_t)(uintptr_t)dc->input_ctx, 0, 0,
        ((uint32_t)XHCI_TRB_TYPE_ADDRESS_DEVICE << XHCI_TRB_TYPE_SHIFT) | ((uint32_t)dc->slot_id << 24), 0);
}

static int xhci_evaluate_ep0_mps(xhci_hc_t* hc, xhci_dev_ctx_t* dc, int new_mps) {
    uint32_t* icc = (uint32_t*)dc->input_ctx;
    icc[0] = 0;
    icc[1] = 0x2;
    uint32_t* ep0_ctx = (uint32_t*)((uint8_t*)dc->input_ctx + 2 * hc->ctx_size);
    ep0_ctx[1] = (ep0_ctx[1] & ~0xFFFF0000u) | ((uint32_t)new_mps << 16);
    return xhci_run_command(hc, (uint32_t)(uintptr_t)dc->input_ctx, 0, 0,
        ((uint32_t)XHCI_TRB_TYPE_CONFIG_EP << XHCI_TRB_TYPE_SHIFT) | ((uint32_t)dc->slot_id << 24), 0) == 0
        ? 0 : -1;
}

static int xhci_do_control(usb_hcd_t* hcdp, usb_device_t* dev, usb_setup_pkt_t* setup, void* buf, int len, int dir_in) {
    xhci_hc_t* hc = (xhci_hc_t*)hcdp->priv;
    xhci_dev_ctx_t* dc = xhci_ensure_slot(hc, dev);
    if (!dc) return -1;

    if (setup->bRequest == USB_REQ_SET_ADDRESS && setup->bmRequestType == 0x00) {
        return xhci_address_device_real(hc, dc) == 0 ? 0 : -1;
    }

    uint32_t trt = (len == 0) ? 0 : (dir_in ? 3u : 2u);
    uint32_t setup_lo = setup->bmRequestType | ((uint32_t)setup->bRequest << 8) | ((uint32_t)setup->wValue << 16);
    uint32_t setup_hi = setup->wIndex | ((uint32_t)setup->wLength << 16);
    xhci_ring_push(dc->ep0_ring, XHCI_XFER_TRB_COUNT, &dc->ep0_enq, &dc->ep0_cycle,
        setup_lo, setup_hi, 8, ((uint32_t)XHCI_TRB_TYPE_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IDT | (trt << 16));

    if (len > 0) {
        xhci_ring_push(dc->ep0_ring, XHCI_XFER_TRB_COUNT, &dc->ep0_enq, &dc->ep0_cycle,
            (uint32_t)(uintptr_t)buf, 0, (uint32_t)len,
            ((uint32_t)XHCI_TRB_TYPE_DATA_STAGE << XHCI_TRB_TYPE_SHIFT) | (dir_in ? XHCI_TRB_DIR_IN : 0));
    }
    uint32_t status_dir = (len > 0 && dir_in) ? 0 : XHCI_TRB_DIR_IN;
    xhci_trb_t* last = xhci_ring_push(dc->ep0_ring, XHCI_XFER_TRB_COUNT, &dc->ep0_enq, &dc->ep0_cycle,
        0, 0, 0, ((uint32_t)XHCI_TRB_TYPE_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC | status_dir);

    hc->db[dc->slot_id] = 1;
    int r = xhci_wait_for_trb(hc, last, 0);
    if (r != 0) return -1;

    if (setup->bRequest == USB_REQ_GET_DESCRIPTOR && (setup->wValue >> 8) == USB_DESC_DEVICE && setup->wLength == 8 && dir_in) {
        uint8_t* b = (uint8_t*)buf;
        int real_mps = b[7] ? b[7] : 8;
        int cur_mps = xhci_default_mps_for_speed(hc->pending_speed_psi);
        if (real_mps != cur_mps) xhci_evaluate_ep0_mps(hc, dc, real_mps);
    }
    return len;
}

static int xhci_ensure_bulk_endpoint(xhci_hc_t* hc, usb_device_t* dev, xhci_dev_ctx_t* dc, int dir_in, int maxpkt) {
    int* configured = dir_in ? &dc->bulk_in_configured : &dc->bulk_out_configured;
    xhci_trb_t** ringp = dir_in ? &dc->bulk_in_ring : &dc->bulk_out_ring;
    int* enqp = dir_in ? &dc->bulk_in_enq : &dc->bulk_out_enq;
    int* cyclep = dir_in ? &dc->bulk_in_cycle : &dc->bulk_out_cycle;
    if (*configured) return 0;

    uint8_t ep_addr = dir_in ? dev->ep_in_addr : dev->ep_out_addr;
    int epnum = ep_addr & 0xF;
    int dci = epnum * 2 + (dir_in ? 1 : 0);

    *ringp = (xhci_trb_t*)pmm_alloc_block();
    if (!*ringp) return -1;
    memset(*ringp, 0, 4096);
    *enqp = 0;
    *cyclep = 1;

    uint32_t* icc = (uint32_t*)dc->input_ctx;
    icc[0] = 0;
    icc[1] = (uint32_t)(1u | (1u << dci));

    uint32_t* slot_ctx = (uint32_t*)((uint8_t*)dc->input_ctx + hc->ctx_size);
    int cur_entries = (int)((slot_ctx[0] >> 27) & 0x1F);
    if (dci > cur_entries) slot_ctx[0] = (slot_ctx[0] & ~(0x1Fu << 27)) | ((uint32_t)dci << 27);

    uint32_t* ep_ctx = (uint32_t*)((uint8_t*)dc->input_ctx + (dci + 1) * hc->ctx_size);
    ep_ctx[0] = 0;
    int ep_type = dir_in ? XHCI_EP_TYPE_BULK_IN : XHCI_EP_TYPE_BULK_OUT;
    ep_ctx[1] = ((uint32_t)ep_type << 3) | (3u << 1) | ((uint32_t)maxpkt << 16);
    ep_ctx[2] = ((uint32_t)(uintptr_t)*ringp) | 1u;
    ep_ctx[3] = 0;
    ep_ctx[4] = maxpkt;

    if (xhci_run_command(hc, (uint32_t)(uintptr_t)dc->input_ctx, 0, 0,
            ((uint32_t)XHCI_TRB_TYPE_CONFIG_EP << XHCI_TRB_TYPE_SHIFT) | ((uint32_t)dc->slot_id << 24), 0) != 0) {
        return -1;
    }

    *configured = 1;
    return 0;
}

static int xhci_bulk_transfer(usb_hcd_t* hcdp, usb_device_t* dev, uint8_t ep_addr, void* buf, int len, int dir_in, int* toggle) {
    (void)ep_addr;
    (void)toggle;
    xhci_hc_t* hc = (xhci_hc_t*)hcdp->priv;
    if (len <= 0 || len > 4096) return -1;

    xhci_dev_ctx_t* dc = xhci_ensure_slot(hc, dev);
    if (!dc) return -1;

    int maxpkt = dir_in ? dev->ep_in_maxpkt : dev->ep_out_maxpkt;
    if (maxpkt <= 0) maxpkt = 64;

    if (xhci_ensure_bulk_endpoint(hc, dev, dc, dir_in, maxpkt) != 0) return -1;

    xhci_trb_t* ring = dir_in ? dc->bulk_in_ring : dc->bulk_out_ring;
    int* enq = dir_in ? &dc->bulk_in_enq : &dc->bulk_out_enq;
    int* cycle = dir_in ? &dc->bulk_in_cycle : &dc->bulk_out_cycle;

    xhci_trb_t* trb = xhci_ring_push(ring, XHCI_XFER_TRB_COUNT, enq, cycle,
        (uint32_t)(uintptr_t)buf, 0, (uint32_t)len,
        ((uint32_t)XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC);

    uint8_t ea = dir_in ? dev->ep_in_addr : dev->ep_out_addr;
    int epnum = ea & 0xF;
    int dci = epnum * 2 + (dir_in ? 1 : 0);
    hc->db[dc->slot_id] = (uint32_t)dci;

    xhci_trb_t ev;
    int loops = XHCI_TIMEOUT_LOOPS;
    int actual = -1;
    while (loops--) {
        if (xhci_process_one_event(hc, &ev)) {
            if (ev.p_lo == (uint32_t)(uintptr_t)trb) {
                int comp = (ev.status >> 24) & 0xFF;
                int remaining = ev.status & 0xFFFFFF;
                actual = (comp == XHCI_COMP_SUCCESS || comp == 13) ? (len - remaining) : -1;
                break;
            }
            continue;
        }
        asm volatile("pause");
    }
    return actual;
}

static int xhci_setup_interrupt_in(usb_hcd_t* hcdp, usb_device_t* dev, uint8_t ep_addr, uint16_t maxpkt, uint8_t interval, void (*callback)(usb_device_t*, uint8_t*, int)) {
    xhci_hc_t* hc = (xhci_hc_t*)hcdp->priv;
    xhci_dev_ctx_t* dc = xhci_ensure_slot(hc, dev);
    if (!dc || g_intr_count >= XHCI_MAX_INTR) return -1;
    if (maxpkt == 0 || maxpkt > 64) maxpkt = 64;

    int epnum = ep_addr & 0xF;
    int dci = epnum * 2 + 1;

    xhci_intr_ctx_t* ic = &g_intr[g_intr_count];
    memset(ic, 0, sizeof(*ic));
    ic->hc = hc;
    ic->dev = dev;
    ic->callback = callback;
    ic->buf = g_intr_bufs[g_intr_count];
    ic->maxpkt = maxpkt;
    ic->dci = dci;
    ic->slot_id = dc->slot_id;
    ic->ring = (xhci_trb_t*)pmm_alloc_block();
    if (!ic->ring) return -1;
    memset(ic->ring, 0, 4096);
    ic->enq = 0;
    ic->cycle = 1;

    uint32_t* icc = (uint32_t*)dc->input_ctx;
    icc[0] = 0;
    icc[1] = (uint32_t)(1u | (1u << dci));

    uint32_t* slot_ctx = (uint32_t*)((uint8_t*)dc->input_ctx + hc->ctx_size);
    int cur_entries = (int)((slot_ctx[0] >> 27) & 0x1F);
    if (dci > cur_entries) slot_ctx[0] = (slot_ctx[0] & ~(0x1Fu << 27)) | ((uint32_t)dci << 27);

    uint32_t* ep_ctx = (uint32_t*)((uint8_t*)dc->input_ctx + (dci + 1) * hc->ctx_size);
    int xinterval = xhci_calc_interval(hc->pending_speed_psi, interval);
    ep_ctx[0] = (uint32_t)xinterval << 16;
    ep_ctx[1] = ((uint32_t)XHCI_EP_TYPE_INTERRUPT_IN << 3) | (3u << 1) | ((uint32_t)maxpkt << 16);
    ep_ctx[2] = ((uint32_t)(uintptr_t)ic->ring) | 1u;
    ep_ctx[3] = 0;
    ep_ctx[4] = maxpkt | ((uint32_t)maxpkt << 16);

    if (xhci_run_command(hc, (uint32_t)(uintptr_t)dc->input_ctx, 0, 0,
            ((uint32_t)XHCI_TRB_TYPE_CONFIG_EP << XHCI_TRB_TYPE_SHIFT) | ((uint32_t)dc->slot_id << 24), 0) != 0) {
        return -1;
    }

    xhci_trb_t* trb = xhci_ring_push(ic->ring, XHCI_XFER_TRB_COUNT, &ic->enq, &ic->cycle,
        (uint32_t)(uintptr_t)ic->buf, 0, maxpkt, ((uint32_t)XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC);
    ic->outstanding = trb;
    ic->active = 1;
    hc->db[dc->slot_id] = (uint32_t)dci;
    g_intr_count++;
    return 0;
}

void xhci_bios_handoff_pci(uint32_t bar0_phys) {
    volatile uint8_t* base = (volatile uint8_t*)(uintptr_t)(bar0_phys & ~0xF);
    uint32_t hccparams1 = *(volatile uint32_t*)(base + XHCI_CAP_HCCPARAMS1);
    uint32_t xecp = (hccparams1 >> 16) & 0xFFFF;
    if (xecp == 0) return;
    volatile uint32_t* cap = (volatile uint32_t*)(base + xecp * 4);
    int loops = 64;
    while (loops--) {
        uint32_t header = *cap;
        if ((header & 0xFF) == 1) {
            if (header & (1u << 16)) {
                *cap = header | (1u << 24);
                int w = 1000000;
                while (w--) {
                    uint32_t v = *cap;
                    if ((v & (1u << 24)) && !(v & (1u << 16))) break;
                    asm volatile("pause");
                }
            }
            return;
        }
        uint32_t next = (header >> 8) & 0xFF;
        if (next == 0) return;
        cap += next;
    }
}

static void xhci_reset_and_route_port(xhci_hc_t* hc, int port) {
    int reg = XHCI_OP_PORTSC_BASE + port * 0x10;
    uint32_t status = op_r(hc, reg);
    if (!(status & XHCI_PORTSC_CCS)) return;

    status = (status & ~XHCI_PORTSC_RW1CS) | XHCI_PORTSC_PR;
    op_w(hc, reg, status);
    xhci_delay(3000000);

    int loops = 200000;
    while (loops--) {
        status = op_r(hc, reg);
        if (status & XHCI_PORTSC_PRC) break;
        asm volatile("pause");
    }
    op_w(hc, reg, (status & ~XHCI_PORTSC_RW1CS) | XHCI_PORTSC_PRC);
    xhci_delay(200000);

    status = op_r(hc, reg);
    if (!(status & (XHCI_PORTSC_CCS))) return;
    if (!(status & XHCI_PORTSC_PED)) return;

    int speed_psi = (int)((status >> XHCI_PORTSC_SPEED_SHIFT) & XHCI_PORTSC_SPEED_MASK);
    hc->pending_port = port + 1;
    hc->pending_speed_psi = speed_psi;
    usb_enumerate_device(&hc->hcd, 0, port, xhci_speed_psi_to_generic(speed_psi));
}

usb_hcd_t* xhci_probe_and_init(uint8_t bus, uint8_t dev, uint8_t func, uint32_t bar0_phys) {
    (void)bus; (void)dev; (void)func;
    if (g_hc_count >= XHCI_MAX_HC) return 0;
    xhci_hc_t* hc = &g_hc[g_hc_count++];
    memset(hc, 0, sizeof(*hc));

    hc->base = (volatile uint8_t*)(uintptr_t)(bar0_phys & ~0xF);
    uint8_t caplength = *(volatile uint8_t*)(hc->base + XHCI_CAP_CAPLENGTH);
    hc->op = hc->base + caplength;
    uint32_t hccparams1 = cap_r(hc, XHCI_CAP_HCCPARAMS1);
    hc->ctx_size = (hccparams1 & (1u << 3)) ? 64 : 32;

    uint32_t dboff = cap_r(hc, XHCI_CAP_DBOFF) & ~0x3u;
    uint32_t rtsoff = cap_r(hc, XHCI_CAP_RTSOFF) & ~0x1Fu;
    hc->db = (volatile uint32_t*)(hc->base + dboff);
    hc->rt = hc->base + rtsoff;

    uint32_t hcsparams1 = cap_r(hc, XHCI_CAP_HCSPARAMS1);
    hc->max_slots = (int)(hcsparams1 & 0xFF);
    if (hc->max_slots > XHCI_MAX_SLOTS) hc->max_slots = XHCI_MAX_SLOTS;
    hc->n_ports = (int)((hcsparams1 >> 24) & 0xFF);
    if (hc->n_ports > USB_MAX_HUB_PORTS) hc->n_ports = USB_MAX_HUB_PORTS;

    op_w(hc, XHCI_OP_USBCMD, op_r(hc, XHCI_OP_USBCMD) & ~XHCI_CMD_RUN);
    int loops = XHCI_TIMEOUT_LOOPS;
    while (loops-- && !(op_r(hc, XHCI_OP_USBSTS) & XHCI_STS_HCHALTED)) asm volatile("pause");

    op_w(hc, XHCI_OP_USBCMD, XHCI_CMD_HCRESET);
    loops = XHCI_TIMEOUT_LOOPS;
    while (loops-- && (op_r(hc, XHCI_OP_USBCMD) & XHCI_CMD_HCRESET)) asm volatile("pause");
    loops = XHCI_TIMEOUT_LOOPS;
    while (loops-- && (op_r(hc, XHCI_OP_USBSTS) & XHCI_STS_CNR)) asm volatile("pause");

    op_w(hc, XHCI_OP_CONFIG, (uint32_t)hc->max_slots);

    hc->dcbaa = (uint32_t*)pmm_alloc_block();
    memset(hc->dcbaa, 0, 4096);

    uint32_t hcsparams2 = cap_r(hc, XHCI_CAP_HCSPARAMS2);
    uint32_t max_scratch = ((hcsparams2 >> 27) & 0x1F) | (((hcsparams2 >> 21) & 0x1F) << 5);
    if (max_scratch > 0) {
        uint32_t* sp_array = (uint32_t*)pmm_alloc_block();
        memset(sp_array, 0, 4096);
        for (uint32_t i = 0; i < max_scratch && i < 1024; i++) {
            void* pg = pmm_alloc_block();
            memset(pg, 0, 4096);
            sp_array[i * 2] = (uint32_t)(uintptr_t)pg;
            sp_array[i * 2 + 1] = 0;
        }
        hc->dcbaa[0] = (uint32_t)(uintptr_t)sp_array;
        hc->dcbaa[1] = 0;
    }
    op_w(hc, XHCI_OP_DCBAAP_LO, (uint32_t)(uintptr_t)hc->dcbaa);
    op_w(hc, XHCI_OP_DCBAAP_HI, 0);

    hc->cmd_ring = (xhci_trb_t*)pmm_alloc_block();
    memset(hc->cmd_ring, 0, 4096);
    hc->cmd_enq = 0;
    hc->cmd_cycle = 1;
    op_w(hc, XHCI_OP_CRCR_LO, (uint32_t)(uintptr_t)hc->cmd_ring | XHCI_CRCR_RCS);
    op_w(hc, XHCI_OP_CRCR_HI, 0);

    hc->evt_ring = (xhci_trb_t*)pmm_alloc_block();
    memset(hc->evt_ring, 0, 4096);
    hc->evt_deq = 0;
    hc->evt_cycle = 1;
    hc->erst = pmm_alloc_block();
    memset(hc->erst, 0, 4096);
    uint32_t* erst = (uint32_t*)hc->erst;
    erst[0] = (uint32_t)(uintptr_t)hc->evt_ring;
    erst[1] = 0;
    erst[2] = XHCI_EVT_TRB_COUNT;
    erst[3] = 0;
    rt_w(hc, XHCI_RT_IR0 + XHCI_IR_ERSTSZ, 1);
    rt_w(hc, XHCI_RT_IR0 + XHCI_IR_ERDP_LO, (uint32_t)(uintptr_t)hc->evt_ring);
    rt_w(hc, XHCI_RT_IR0 + XHCI_IR_ERDP_HI, 0);
    rt_w(hc, XHCI_RT_IR0 + XHCI_IR_ERSTBA_LO, (uint32_t)(uintptr_t)hc->erst);
    rt_w(hc, XHCI_RT_IR0 + XHCI_IR_ERSTBA_HI, 0);

    op_w(hc, XHCI_OP_USBCMD, op_r(hc, XHCI_OP_USBCMD) | XHCI_CMD_RUN);
    loops = XHCI_TIMEOUT_LOOPS;
    while (loops-- && (op_r(hc, XHCI_OP_USBSTS) & XHCI_STS_HCHALTED)) asm volatile("pause");

    hc->hcd.control_transfer = xhci_do_control;
    hc->hcd.setup_interrupt_in = xhci_setup_interrupt_in;
    hc->hcd.bulk_transfer = xhci_bulk_transfer;
    hc->hcd.priv = hc;

    klog_status("XHCI CONTROLLER STARTED", 0x00FF00);

    for (int port = 0; port < hc->n_ports; port++) {
        xhci_reset_and_route_port(hc, port);
    }

    return &hc->hcd;
}