#ifndef UHCI_H
#define UHCI_H
#include <stdint.h>
#include "usb.h"

usb_hcd_t* uhci_probe_and_init(uint8_t bus, uint8_t dev, uint8_t func, uint16_t io_base);
void uhci_poll_interrupts(void);

#endif
