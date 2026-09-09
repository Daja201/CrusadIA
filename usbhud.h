#ifndef USBHID_H
#define USBHID_H
#include "usb.h"
#include <stdint.h>

void usbhid_attach(usb_device_t* dev);
int usbhid_raw_slot_count(void);
usb_device_t* usbhid_raw_slot_device(int index);
int usbhid_raw_slot_report(int index, uint8_t* out_buf, int max_len, uint32_t* out_update_count);

#endif