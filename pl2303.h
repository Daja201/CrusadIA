#ifndef PL2303_H
#define PL2303_H
#include "usb.h"
#include <stdint.h>

#define PL2303_PARITY_NONE 0
#define PL2303_PARITY_ODD  1
#define PL2303_PARITY_EVEN 2

int pl2303_probe(usb_device_t* dev);
void pl2303_attach(usb_device_t* dev);
int pl2303_set_line(usb_device_t* dev, uint32_t baud, uint8_t databits, uint8_t parity, uint8_t stopbits);
int pl2303_set_control_lines(usb_device_t* dev, int dtr, int rts);
int pl2303_write(usb_device_t* dev, const uint8_t* data, int len);
int pl2303_read(usb_device_t* dev, uint8_t* buf, int maxlen);
usb_device_t* pl2303_get_active(void);

#endif
