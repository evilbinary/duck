/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * USB Core Header
 ********************************************************************/
#ifndef USB_H
#define USB_H

#include "kernel/kernel.h"

// USB 速度
#define USB_SPEED_LOW     0
#define USB_SPEED_FULL    1
#define USB_SPEED_HIGH    2

// USB 端点类型
#define USB_ENDPOINT_CONTROL     0
#define USB_ENDPOINT_ISOCHRONOUS 1
#define USB_ENDPOINT_BULK        2
#define USB_ENDPOINT_INTERRUPT   3

// USB 请求类型
#define USB_REQUEST_TYPE_HOST_TO_DEVICE     0x00
#define USB_REQUEST_TYPE_DEVICE_TO_HOST     0x80
#define USB_REQUEST_TYPE_TYPE_MASK          0x60
#define USB_REQUEST_TYPE_TYPE_STANDARD      0x00
#define USB_REQUEST_TYPE_TYPE_CLASS         0x20
#define USB_REQUEST_TYPE_TYPE_VENDOR        0x40
#define USB_REQUEST_TYPE_CLASS              0x20  // 简写
#define USB_REQUEST_TYPE_RECIPIENT_MASK      0x1F
#define USB_REQUEST_TYPE_RECIPIENT_DEVICE    0x00
#define USB_REQUEST_TYPE_RECIPIENT_INTERFACE 0x01
#define USB_REQUEST_TYPE_RECIPIENT_ENDPOINT  0x02
#define USB_REQUEST_TYPE_RECIPIENT_OTHER     0x03

// USB 标准请求
#define USB_REQUEST_GET_STATUS          0x00
#define USB_REQUEST_CLEAR_FEATURE       0x01
#define USB_REQUEST_SET_FEATURE         0x03
#define USB_REQUEST_SET_ADDRESS         0x05
#define USB_REQUEST_GET_DESCRIPTOR      0x06
#define USB_REQUEST_SET_DESCRIPTOR      0x07
#define USB_REQUEST_GET_CONFIGURATION  0x08
#define USB_REQUEST_SET_CONFIGURATION  0x09
#define USB_REQUEST_GET_INTERFACE       0x0A
#define USB_REQUEST_SET_INTERFACE       0x0B
#define USB_REQUEST_SYNCH_FRAME         0x0C

// 描述符类型
#define USB_DESCRIPTOR_TYPE_DEVICE           0x01
#define USB_DESCRIPTOR_TYPE_CONFIGURATION    0x02
#define USB_DESCRIPTOR_TYPE_STRING          0x03
#define USB_DESCRIPTOR_TYPE_INTERFACE        0x04
#define USB_DESCRIPTOR_TYPE_ENDPOINT         0x05
#define USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER 0x06
#define USB_DESCRIPTOR_TYPE_OTHER_SPEED      0x07
#define USB_DESCRIPTOR_TYPE_INTERFACE_POWER  0x08

// HID 描述符类型
#define USB_DESCRIPTOR_TYPE_HID         0x21
#define USB_DESCRIPTOR_TYPE_REPORT      0x22
#define USB_DESCRIPTOR_TYPE_PHYSICAL    0x23

// Hub 描述符类型
#define USB_DESCRIPTOR_TYPE_HUB         0x29

// HID 类请求
#define USB_HID_REQUEST_GET_REPORT   0x01
#define USB_HID_REQUEST_GET_IDLE     0x02
#define USB_HID_REQUEST_GET_PROTOCOL  0x03
#define USB_HID_REQUEST_SET_REPORT   0x09
#define USB_HID_REQUEST_SET_IDLE     0x0A
#define USB_HID_REQUEST_SET_PROTOCOL  0x0B

// USB 类别
#define USB_CLASS_PER_INTERFACE      0x00
#define USB_CLASS_AUDIO              0x01
#define USB_CLASS_COMM               0x02
#define USB_CLASS_HID                0x03
#define USB_CLASS_PRINTER            0x07
#define USB_CLASS_MASS_STORAGE       0x08
#define USB_CLASS_HUB                0x09
#define USB_CLASS_DATA               0x0A
#define USB_CLASS_SMART_CARD         0x0B
#define USB_CLASS_CONTENT_SECURITY   0x0D
#define USB_CLASS_VIDEO              0x0E
#define USB_CLASS_PERSONAL_HEALTHCARE 0x0F
#define USB_CLASS_DIAGNOSTIC_DEVICE  0xDC
#define USB_CLASS_WIRELESS_CONTROLLER 0xE0
#define USB_CLASS_MISCELLANEOUS      0xEF
#define USB_CLASS_APPLICATION_SPECIFIC 0xFE
#define USB_CLASS_VENDOR_SPECIFIC    0xFF

// USB 端口状态
#define USB_PORT_CONNECTION    0x0001
#define USB_PORT_ENABLE        0x0002
#define USB_PORT_SUSPEND       0x0004
#define USB_PORT_OVER_CURRENT  0x0008
#define USB_PORT_RESET         0x0010
#define USB_PORT_POWER         0x0100
#define USB_PORT_LOW_SPEED     0x0200
#define USB_PORT_HIGH_SPEED    0x0400
#define USB_PORT_TEST          0x0800
#define USB_PORT_INDICATOR     0x1000

// URB 状态
#define URB_OK         0
#define URB_PENDING    1
#define URB_ERROR      2
#define URB_STALL      3
#define URB_NOQUEUE    4

// 端点描述符
typedef struct usb_endpoint_descriptor {
    u8  length;
    u8  type;
    u8  address;
    u8  attributes;
    u16 max_packet_size;
    u8  interval;
    u8  refresh;
    u8  synch_address;
} usb_endpoint_descriptor_t;

// 设备结构
typedef struct usb_device {
    u8 address;
    u8 speed;
    u8 port;
    u8 parent;
    u16 vendor;
    u16 product;
    u8 class;
    u8 subclass;
    u8 protocol;
    u8 maxchild;
    u8 config;
    u8 num_endpoints;
    usb_endpoint_descriptor_t ep[16];
    char name[32];
    void* hc_private;
    struct usb_device* next;
} usb_device_t;

// 接口描述符
typedef struct usb_interface_descriptor {
    u8  length;
    u8  type;
    u8  interface_number;
    u8  alternate_setting;
    u8  num_endpoints;
    u8  interface_class;
    u8  interface_subclass;
    u8  interface_protocol;
    u8  interface_index;
} usb_interface_descriptor_t;

// 配置描述符
typedef struct usb_configuration_descriptor {
    u8  length;
    u8  type;
    u16 total_length;
    u8  num_interfaces;
    u8  configuration_value;
    u8  configuration_index;
    u8  attributes;
    u8  max_power;
} usb_configuration_descriptor_t;

// 设备描述符
typedef struct usb_device_descriptor {
    u8  length;
    u8  type;
    u16 bcd_usb;
    u8  device_class;
    u8  device_subclass;
    u8  device_protocol;
    u8  max_packet_size_0;
    u16 id_vendor;
    u16 id_product;
    u16 bcd_device;
    u8  manufacturer_index;
    u8  product_index;
    u8  serial_number_index;
    u8  num_configurations;
} usb_device_descriptor_t;

// HID 描述符
typedef struct usb_hid_descriptor {
    u8  length;
    u8  type;
    u16 bcd_hid;
    u8  country_code;
    u8  num_descriptors;
    u8  descriptor_type;
    u16 descriptor_length;
} usb_hid_descriptor_t;

// URB (USB Request Block)
typedef struct urb {
    u8* transfer_buffer;
    u32 transfer_length;
    u8  endpoint;
    u8  device_address;
    u8  request_type;
    u8  request;
    u16 value;
    u16 index;
    u16 length;
    void (*complete)(struct urb*);
    void* hc_private;
    int status;
    int actual_length;
} urb_t;

// USB 主机控制器驱动接口
typedef struct hcd_ops {
    int (*init)(void);
    int (*submit_urb)(urb_t* urb);
    int (*get_frame_number)(void);
    void (*shutdown)(void);
} hcd_ops_t;

// HID 报告
typedef struct hid_report {
    u8 report_id;
    u8* data;
    u32 length;
} hid_report_t;

// USB 鼠标事件
typedef struct usb_mouse_event {
    u8 buttons;
    i8 x;
    i8 y;
    i8 wheel;
} usb_mouse_event_t;

// USB 核心函数
void usb_init(void);
void usb_exit(void);
int usb_alloc_device(usb_device_t* dev);
int usb_free_device(usb_device_t* dev);
int usb_enumerate(void);
usb_device_t* usb_find_device(u8 address);
usb_device_t* usb_find_device_by_vid_pid(u16 vendor, u16 product);
int usb_control_transfer(u8 device_address, u8 endpoint, 
                        u8 request_type, u8 request,
                        u16 value, u16 index, void* data, u16 len, u32 timeout);
int usb_interrupt_transfer(usb_device_t* dev, u8 endpoint, 
                         void* data, u16 len);
int usb_bulk_transfer(usb_device_t* dev, u8 endpoint,
                     void* data, u32 len);

// HID 函数
void hid_init(void);
int hid_parse_report(usb_device_t* dev, u8* report_desc, u32 length);
int hid_get_report(usb_device_t* dev, u8 report_id, void* buf, u32 len);
int hid_set_report(usb_device_t* dev, u8 report_id, void* buf, u32 len);

// USB 鼠标函数
void usb_mouse_init(void);
void usb_mouse_exit(void);
int usb_mouse_connect(usb_device_t* dev);
int usb_mouse_disconnect(usb_device_t* dev);
void usb_mouse_handler(urb_t* urb);
int usb_mouse_parse_report(u8* data, u32 len, usb_mouse_event_t* event);

// USB Hub 函数
void hub_init(void);
int hub_probe(usb_device_t* dev);
int hub_disconnect(usb_device_t* dev);
void hub_port_status_changed(usb_device_t* hub, u8 port);

// HCD 接口
int hcd_init(void);
void hcd_shutdown(void);
int hcd_submit_urb(urb_t* urb);
int hcd_unlink_urb(urb_t* urb);
int hcd_get_frame_number(void);
void hcd_register_ops(hcd_ops_t* ops);

// USB 调试
#define USB_DEBUG(fmt, ...) kprintf("[USB] " fmt, ##__VA_ARGS__)
#define USB_INFO(fmt, ...) kprintf("[USB] " fmt, ##__VA_ARGS__)
#define USB_ERROR(fmt, ...) kprintf("[USB ERROR] " fmt, ##__VA_ARGS__)
#define USB_WARN(fmt, ...) kprintf("[USB WARN] " fmt, ##__VA_ARGS__)

#endif
