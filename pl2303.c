/* Prolific PL2303 USB<->RS232 bridge driver.
 *
 * Tested against: Bus 001 Device 010: ID 067b:23a3 Prolific Technology,
 * Inc. ATEN Serial Bridge cable (a PL2303-based dongle sold under ATEN's
 * own PID). The interface is vendor-specific (bInterfaceClass 0xFF) with
 * one bulk-IN, one bulk-OUT and (on most units) one interrupt-IN endpoint
 * for modem status - we only need the two bulk endpoints for raw data.
 *
 * Vendor requests (bRequest 0x01 read/write of internal registers, plus
 * the CDC-style SET_LINE_CODING/SET_CONTROL_LINE_STATE class requests on
 * the communication interface) follow the well documented PL2303 wire
 * protocol used by every open-source PL2303 driver.
 */
#include "pl2303.h"
#include "usb.h"
#include "klog.h"
#include "string.h"

#define PL2303_VENDOR_REQUEST   0x01
#define PL2303_VENDOR_WRITE_TYPE 0x40  /* host->device, vendor, device */
#define PL2303_VENDOR_READ_TYPE  0xC0  /* device->host, vendor, device */

#define PL2303_SET_LINE_TYPE    0x21   /* host->device, class, interface */
#define PL2303_SET_LINE_REQUEST 0x20
#define PL2303_SET_CONTROL_REQUEST 0x22
#define PL2303_CTRL_DTR (1 << 0)
#define PL2303_CTRL_RTS (1 << 1)

typedef struct {
    uint16_t vid;
    uint16_t pid;
    const char* name;
} pl2303_id_t;

static const pl2303_id_t k_known_ids[] = {
    { 0x067B, 0x2303, "Prolific PL2303" },
    { 0x067B, 0x23A3, "Prolific PL2303 (ATEN Serial Bridge cable)" },
    { 0x067B, 0x23B3, "Prolific PL2303HXD" },
    { 0x067B, 0x23C3, "Prolific PL2303HXD (GC)" },
    { 0x067B, 0x23D3, "Prolific PL2303HXD (GC)" },
    { 0x067B, 0x23E3, "Prolific PL2303HXD (GC)" },
    { 0x0557, 0x2008, "ATEN UC-232A (PL2303)" },
};
#define PL2303_KNOWN_ID_COUNT (int)(sizeof(k_known_ids) / sizeof(k_known_ids[0]))

static usb_device_t* g_active = 0;

int pl2303_probe(usb_device_t* dev) {
    if (!dev) return 0;
    uint16_t vid = dev->dev_desc.idVendor;
    uint16_t pid = dev->dev_desc.idProduct;
    for (int i = 0; i < PL2303_KNOWN_ID_COUNT; i++) {
        if (k_known_ids[i].vid == vid && k_known_ids[i].pid == pid) return 1;
    }
    return 0;
}

static const char* pl2303_name(usb_device_t* dev) {
    for (int i = 0; i < PL2303_KNOWN_ID_COUNT; i++) {
        if (k_known_ids[i].vid == dev->dev_desc.idVendor && k_known_ids[i].pid == dev->dev_desc.idProduct) {
            return k_known_ids[i].name;
        }
    }
    return "PL2303-compatible";
}

static int pl2303_vendor_write(usb_device_t* dev, uint16_t value, uint16_t index) {
    return usb_control_transfer(dev, PL2303_VENDOR_WRITE_TYPE, PL2303_VENDOR_REQUEST, value, index, 0, 0);
}

static int pl2303_vendor_read(usb_device_t* dev, uint16_t value, uint8_t* out) {
    uint8_t buf[1] = {0};
    int r = usb_control_transfer(dev, PL2303_VENDOR_READ_TYPE, PL2303_VENDOR_REQUEST, value, 0, buf, 1);
    if (out) *out = buf[0];
    return r;
}

int pl2303_set_control_lines(usb_device_t* dev, int dtr, int rts) {
    if (!dev) return -1;
    uint16_t mask = (dtr ? PL2303_CTRL_DTR : 0) | (rts ? PL2303_CTRL_RTS : 0);
    return usb_control_transfer(dev, PL2303_SET_LINE_TYPE, PL2303_SET_CONTROL_REQUEST, mask, 0, 0, 0);
}

int pl2303_set_line(usb_device_t* dev, uint32_t baud, uint8_t databits, uint8_t parity, uint8_t stopbits) {
    if (!dev) return -1;
    uint8_t line[7];
    line[0] = (uint8_t)(baud & 0xFF);
    line[1] = (uint8_t)((baud >> 8) & 0xFF);
    line[2] = (uint8_t)((baud >> 16) & 0xFF);
    line[3] = (uint8_t)((baud >> 24) & 0xFF);
    line[4] = stopbits; /* 0 = 1 stop bit, 1 = 1.5, 2 = 2 */
    line[5] = parity;   /* 0 = none, 1 = odd, 2 = even */
    line[6] = databits; /* 5,6,7,8 */
    return usb_control_transfer(dev, PL2303_SET_LINE_TYPE, PL2303_SET_LINE_REQUEST, 0, 0, line, sizeof(line));
}

/* The magic register poke sequence every PL2303 needs after enumeration
   before it will actually pass data. Values/order match the adapter's
   documented type-0 (HX-family) init handshake. */
static void pl2303_vendor_init(usb_device_t* dev) {
    uint8_t tmp;
    pl2303_vendor_read(dev, 0x8484, &tmp);
    pl2303_vendor_write(dev, 0x0404, 0);
    pl2303_vendor_read(dev, 0x8484, &tmp);
    pl2303_vendor_read(dev, 0x8383, &tmp);
    pl2303_vendor_read(dev, 0x8484, &tmp);
    pl2303_vendor_write(dev, 0x0404, 1);
    pl2303_vendor_read(dev, 0x8484, &tmp);
    pl2303_vendor_read(dev, 0x8383, &tmp);
    pl2303_vendor_write(dev, 0, 1);
    pl2303_vendor_write(dev, 1, 0);
    pl2303_vendor_write(dev, 2, 0x24);
}

void pl2303_attach(usb_device_t* dev) {
    if (!dev) return;

    if (!dev->ep_out_addr || !dev->ep_in_addr) {
        klog_color("pl2303: adapter has no usable bulk endpoints, aborting attach\n", 0xFF0000);
        return;
    }

    pl2303_vendor_init(dev);

    /* nEXT pumps are fixed at 9600 baud, 8 data bits, 1 stop bit, no
       parity - configure that as the power-on default so `pumpon`/
       `pumpcmd` work immediately without extra setup. */
    pl2303_set_line(dev, 9600, 8, PL2303_PARITY_NONE, 0);
    pl2303_set_control_lines(dev, 1, 1);

    g_active = dev;

    klogf_color("pl2303: %s attached (vid=0x%x pid=0x%x, ep_in=0x%x ep_out=0x%x), 9600 8N1\n",
        0x00FF00, pl2303_name(dev), dev->dev_desc.idVendor, dev->dev_desc.idProduct,
        dev->ep_in_addr, dev->ep_out_addr);
}

int pl2303_write(usb_device_t* dev, const uint8_t* data, int len) {
    if (!dev) return -1;
    return usb_bulk_write(dev, data, len);
}

int pl2303_read(usb_device_t* dev, uint8_t* buf, int maxlen) {
    if (!dev) return -1;
    return usb_bulk_read(dev, buf, maxlen);
}

usb_device_t* pl2303_get_active(void) {
    return g_active;
}
