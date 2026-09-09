#ifndef USB_H
#define USB_H
#include <stdint.h>

#define USB_MAX_DEVICES 32
#define USB_MAX_HUB_PORTS 8
#define USB_MAX_ENDPOINTS 8

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} __attribute__((packed)) usb_device_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} __attribute__((packed)) usb_config_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} __attribute__((packed)) usb_interface_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} __attribute__((packed)) usb_endpoint_descriptor_t;

typedef struct {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_pkt_t;

#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_CLEAR_FEATURE     0x01
#define USB_REQ_SET_FEATURE       0x03
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_SET_INTERFACE     0x0B
#define USB_REQ_SET_PROTOCOL      0x0B
#define USB_REQ_SET_IDLE          0x0A

#define USB_DESC_DEVICE           0x01
#define USB_DESC_CONFIGURATION    0x02
#define USB_DESC_STRING           0x03
#define USB_DESC_INTERFACE        0x04
#define USB_DESC_ENDPOINT         0x05
#define USB_DESC_HUB              0x29

#define USB_CLASS_HUB             0x09
#define USB_CLASS_HID             0x03

struct usb_hcd;

typedef struct usb_device {
    int in_use;
    int address;
    int speed;
    int max_packet0;
    struct usb_hcd* hcd;
    int hub_addr;
    int hub_port;
    usb_device_descriptor_t dev_desc;
    uint8_t iface_class;
    uint8_t iface_subclass;
    uint8_t iface_protocol;
    uint8_t ep_in_addr;
    uint16_t ep_in_maxpkt;
    uint8_t ep_in_interval;
    void* hcd_priv;
} usb_device_t;

typedef struct usb_hcd {
    int (*control_transfer)(struct usb_hcd* hcd, usb_device_t* dev, usb_setup_pkt_t* setup, void* buf, int len, int dir_in);
    int (*setup_interrupt_in)(struct usb_hcd* hcd, usb_device_t* dev, uint8_t ep_addr, uint16_t maxpkt, uint8_t interval, void (*callback)(usb_device_t*, uint8_t*, int));
    void* priv;
} usb_hcd_t;

void usb_init(void);
usb_device_t* usb_alloc_device(usb_hcd_t* hcd);
void usb_free_device(usb_device_t* dev);
int usb_control_transfer(usb_device_t* dev, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, void* buf, uint16_t wLength);
int usb_get_descriptor(usb_device_t* dev, uint8_t type, uint8_t index, void* buf, uint16_t len);
int usb_set_address(usb_device_t* dev, int addr);
int usb_set_configuration(usb_device_t* dev, int config);
void usb_enumerate_device(usb_hcd_t* hcd, int hub_addr, int hub_port, int speed);
void usb_poll(void);
int usb_device_count(void);
usb_device_t* usb_get_device(int index);
const char* usb_speed_str(int speed);
const char* usb_class_str(uint8_t class_code);

#endif