#ifndef USBHUB_H
#define USBHUB_H
#include "usb.h"

void usbhub_attach(usb_device_t* dev);
void usbhub_poll(void);

#endif
