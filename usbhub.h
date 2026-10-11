#ifndef USBHID_H
#define USBHID_H
#include "usb.h"
#include <stdint.h>

extern volatile int mouse_x, mouse_y;
extern volatile uint8_t mouse_buttons;
extern volatile int mouse_dirty;
int mouse_poll_click(int* out_x, int* out_y);
void usbhid_attach(usb_device_t* dev);

#endif