/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * USB Mouse Driver
 ********************************************************************/
#include "usb.h"
#include "mouse/mouse.h"
#include "dev/devfs.h"

// USB 鼠标设备
typedef struct usb_mouse_device {
    usb_device_t* usb_dev;
    u8 endpoint_in;
    u16 max_packet_size;
    u8 protocol;      // 0 = 协议, 1 = 报告, 2 = 引导协议
    u8 idle_state;
    u8 report_buffer[8];
    struct usb_mouse_device* next;
} usb_mouse_device_t;

static usb_mouse_device_t* usb_mice = NULL;

// USB 鼠标报告描述符 (Boot Protocol)
static const u8 usb_mouse_report_desc[] = {
    // Usage Page (Generic Desktop)
    0x05, 0x01,
    // Usage (Mouse)
    0x09, 0x02,
    // Collection (Application)
    0xA1, 0x01,
    //   Usage (Pointer)
    0x09, 0x01,
    //   Collection (Physical)
    0xA1, 0x00,
    //     Usage Page (Buttons)
    0x05, 0x09,
    //     Usage Minimum (1)
    0x19, 0x01,
    //     Usage Maximum (3)
    0x29, 0x03,
    //     Logical Minimum (0)
    0x15, 0x00,
    //     Logical Maximum (1)
    0x25, 0x01,
    //     Report Size (1)
    0x75, 0x01,
    //     Report Count (3)
    0x95, 0x03,
    //     Input (Data, Variable, Absolute)
    0x81, 0x02,
    //     Report Size (5)
    0x75, 0x05,
    //     Report Count (1)
    0x95, 0x01,
    //     Input (Constant)
    0x81, 0x01,
    //     Usage Page (Generic Desktop)
    0x05, 0x01,
    //     Usage (X)
    0x09, 0x30,
    //     Usage (Y)
    0x09, 0x31,
    //     Usage (Wheel)
    0x09, 0x38,
    //     Logical Minimum (-127)
    0x15, 0x81,
    //     Logical Maximum (127)
    0x25, 0x7F,
    //     Report Size (8)
    0x75, 0x08,
    //     Report Count (3)
    0x95, 0x03,
    //     Input (Data, Variable, Relative)
    0x81, 0x06,
    //   End Collection
    0xC0,
    // End Collection
    0xC0
};

// USB 鼠标事件回调
static void usb_mouse_event_handler(u8* data, u32 length) {
    if (data == NULL || length < 3) return;
    
    usb_mouse_event_t event;
    kmemset(&event, 0, sizeof(event));
    
    // 解析 USB 鼠标报告
    // Byte 0: 按钮
    event.buttons = data[0];
    
    // Byte 1: X 移动
    event.x = (i8)data[1];

    // Byte 2: Y 移动
    event.y = (i8)data[2];

    // Byte 3: 滚轮 (如果有)
    if (length > 3) {
        event.wheel = (i8)data[3];
    }
    
    USB_DEBUG("USB Mouse: buttons=%02x x=%d y=%d wheel=%d\n",
              event.buttons, event.x, event.y, event.wheel);
    
    // 转换并发送到鼠标设备
    // 实际实现需要调用 mouse 驱动
}

// 设置引导协议
static int usb_mouse_set_protocol(usb_device_t* dev, u8 protocol) {
    return usb_control_transfer(dev->address, 0,
                               USB_REQUEST_TYPE_HOST_TO_DEVICE | USB_REQUEST_TYPE_CLASS | USB_REQUEST_TYPE_RECIPIENT_INTERFACE,
                               USB_HID_REQUEST_SET_PROTOCOL,
                               protocol, 0, NULL, 0, 1000);
}

// 设置空闲
static int usb_mouse_set_idle(usb_device_t* dev, u8 duration) {
    return usb_control_transfer(dev->address, 0,
                               USB_REQUEST_TYPE_HOST_TO_DEVICE | USB_REQUEST_TYPE_CLASS | USB_REQUEST_TYPE_RECIPIENT_INTERFACE,
                               USB_HID_REQUEST_SET_IDLE,
                               (duration << 8) | 0, 0, NULL, 0, 1000);
}

// 获取报告 (用于轮询模式)
static int usb_mouse_get_report(usb_device_t* dev, u8* buffer, u16 length) {
    return usb_control_transfer(dev->address, 0,
                               USB_REQUEST_TYPE_DEVICE_TO_HOST | USB_REQUEST_TYPE_CLASS | USB_REQUEST_TYPE_RECIPIENT_INTERFACE,
                               USB_HID_REQUEST_GET_REPORT,
                               0x0100, 0, buffer, length, 100);
}

// USB 鼠标连接
int usb_mouse_connect(usb_device_t* dev) {
    if (dev == NULL) return -1;
    
    USB_INFO("USB mouse connected: vid=%04x pid=%04x\n", dev->vendor, dev->product);
    
    // 查找中断 IN 端点
    u8 endpoint_in = 0;
    u16 max_packet = 8;
    
    for (int i = 0; i < dev->num_endpoints; i++) {
        u8 addr = dev->ep[i].address;
        u8 attr = dev->ep[i].attributes;
        
        if ((attr & 0x03) == USB_ENDPOINT_INTERRUPT) {
            if ((addr & 0x80) == 0x80) {  // IN 端点
                endpoint_in = addr;
                max_packet = dev->ep[i].max_packet_size;
                USB_INFO("Found interrupt IN endpoint: addr=%02x maxpacket=%d\n",
                         addr, max_packet);
                break;
            }
        }
    }
    
    if (endpoint_in == 0) {
        USB_ERROR("No interrupt IN endpoint found\n");
        return -1;
    }
    
    // 设置引导协议
    usb_mouse_set_protocol(dev, 0);  // 0 = Boot Protocol
    
    // 设置空闲速率
    usb_mouse_set_idle(dev, 10);  // 10ms
    
    // 分配 USB 鼠标设备
    usb_mouse_device_t* mouse = kmalloc(sizeof(usb_mouse_device_t), KERNEL_TYPE);
    if (mouse == NULL) {
        USB_ERROR("Failed to allocate USB mouse device\n");
        return -1;
    }
    
    kmemset(mouse, 0, sizeof(usb_mouse_device_t));
    mouse->usb_dev = dev;
    mouse->endpoint_in = endpoint_in;
    mouse->max_packet_size = max_packet;
    mouse->protocol = 1;  // 报告协议
    mouse->idle_state = 10;
    
    // 添加到鼠标列表
    mouse->next = usb_mice;
    usb_mice = mouse;
    
    USB_INFO("USB mouse initialized\n");
    
    // 注册设备到 devfs
    device_t* mouse_dev = kmalloc(sizeof(device_t), KERNEL_TYPE);
    mouse_dev->name = "usb_mouse";
    mouse_dev->id = DEVICE_MOUSE;
    mouse_dev->type = DEVICE_TYPE_CHAR;
    device_add(mouse_dev);
    
    return 0;
}

// USB 鼠标断开
int usb_mouse_disconnect(usb_device_t* dev) {
    if (dev == NULL) return -1;
    
    USB_INFO("USB mouse disconnected\n");
    
    // 从鼠标列表移除
    usb_mouse_device_t* prev = NULL;
    usb_mouse_device_t* curr = usb_mice;
    
    while (curr != NULL) {
        if (curr->usb_dev == dev) {
            if (prev == NULL) {
                usb_mice = curr->next;
            } else {
                prev->next = curr->next;
            }
            kfree(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    
    return 0;
}

// 处理 URB
void usb_mouse_handler(urb_t* urb) {
    if (urb == NULL || urb->status != URB_OK) {
        return;
    }
    
    usb_mouse_event_handler(urb->transfer_buffer, urb->actual_length);
}

// 解析报告
int usb_mouse_parse_report(u8* data, u32 len, usb_mouse_event_t* event) {
    if (data == NULL || event == NULL || len < 3) {
        return -1;
    }
    
    kmemset(event, 0, sizeof(usb_mouse_event_t));
    
    event->buttons = data[0];
    event->x = (i8)data[1];
    event->y = (i8)data[2];

    if (len > 3) {
        event->wheel = (i8)data[3];
    }
    
    return 0;
}

// USB 鼠标轮询 (需要在主循环中调用)
void usb_mouse_poll(void) {
    usb_mouse_device_t* mouse = usb_mice;
    
    while (mouse != NULL) {
        // 从中断端点读取数据
        int ret = usb_interrupt_transfer(mouse->usb_dev, mouse->endpoint_in,
                                        mouse->report_buffer, mouse->max_packet_size);
        
        if (ret >= 0) {
            usb_mouse_event_handler(mouse->report_buffer, ret);
        }
        
        mouse = mouse->next;
    }
}

// USB 鼠标初始化
void usb_mouse_init(void) {
    USB_INFO("USB mouse driver initializing...\n");
    usb_mice = NULL;
    USB_INFO("USB mouse driver initialized\n");
}

// USB 鼠标退出
void usb_mouse_exit(void) {
    USB_INFO("USB mouse driver exiting...\n");
    
    // 释放所有鼠标设备
    usb_mouse_device_t* mouse = usb_mice;
    while (mouse != NULL) {
        usb_mouse_device_t* next = mouse->next;
        kfree(mouse);
        mouse = next;
    }
    usb_mice = NULL;
    
    USB_INFO("USB mouse driver exited\n");
}

// 模块
module_t usb_mouse_module = {
    .name = "usb_mouse",
    .init = usb_mouse_init,
    .exit = usb_mouse_exit
};
