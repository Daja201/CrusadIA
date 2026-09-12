#ifndef PL2303_H
#define PL2303_H
#include "usb.h"
#include <stdint.h>

/* Prolific PL2303 based USB<->RS232 bridges. Covers the plain Prolific
   VID/PID as well as the common rebadges (ATEN UC-232A/UC-232A1 dongles
   and the ATEN "Serial Bridge" cable variant, e.g. 067b:23a3, that ships
   under ATEN's own product line but uses the stock PL2303 silicon). */

#define PL2303_PARITY_NONE 0
#define PL2303_PARITY_ODD  1
#define PL2303_PARITY_EVEN 2

/* Returns 1 if this device looks like a PL2303-family USB-serial adapter. */
int pl2303_probe(usb_device_t* dev);

/* Runs the PL2303 vendor init sequence, configures 9600 8N1 (the fixed
   nEXT pump serial setting), raises DTR/RTS and registers the device as
   the active USB-serial port. */
void pl2303_attach(usb_device_t* dev);

/* Reconfigure line settings on an already-attached adapter. */
int pl2303_set_line(usb_device_t* dev, uint32_t baud, uint8_t databits, uint8_t parity, uint8_t stopbits);
int pl2303_set_control_lines(usb_device_t* dev, int dtr, int rts);

/* Raw byte-level I/O over the adapter's bulk endpoints. */
int pl2303_write(usb_device_t* dev, const uint8_t* data, int len);
int pl2303_read(usb_device_t* dev, uint8_t* buf, int maxlen);

/* Most callers only ever have one USB-serial dongle plugged in (this is
   what commands.c and the pump driver use); this hands back whichever
   PL2303 attached most recently, or 0 if none is present. */
usb_device_t* pl2303_get_active(void);

#endif
