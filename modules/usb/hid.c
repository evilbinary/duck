/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * USB HID Driver
 ********************************************************************/
#include "usb.h"
#include "mouse/mouse.h"

// HID 设备结构
typedef struct hid_device {
    usb_device_t* usb_dev;
    u8 interface;
    u8 report_id;
    u16 report_size;
    u8* report_desc;
    u32 report_desc_length;
    void (*report_callback)(u8* data, u32 length);
    struct hid_device* next;
} hid_device_t;

static hid_device_t* hid_devices = NULL;

// HID 报告描述符解析
typedef struct hid_report_item {
    u8 size;
    u8 type;
    u8 tag;
    u32 value;
} hid_report_item_t;

// HID 报告项类型
#define HID_ITEM_TYPE_MAIN    0
#define HID_ITEM_TYPE_GLOBAL  1
#define HID_ITEM_TYPE_LOCAL  2

// HID 报告项标签 (Main)
#define HID_MAIN_INPUT       0x08
#define HID_MAIN_OUTPUT      0x09
#define HID_MAIN_FEATURE     0x0B
#define HID_MAIN_COLLECTION  0x0A
#define HID_MAIN_END_COLLECTION 0xC

// HID 报告项标签 (Global)
#define HID_GLOBAL_USAGE_PAGE   0x04
#define HID_GLOBAL_LOGICAL_MIN  0x14
#define HID_GLOBAL_LOGICAL_MAX  0x24
#define HID_GLOBAL_PHYSICAL_MIN 0x34
#define HID_GLOBAL_PHYSICAL_MAX 0x44
#define HID_GLOBAL_UNIT_EXPONENT 0x54
#define HID_GLOBAL_UNIT         0x64
#define HID_GLOBAL_REPORT_SIZE  0x74
#define HID_GLOBAL_REPORT_ID    0x84
#define HID_GLOBAL_REPORT_COUNT 0x94
#define HID_GLOBAL_PUSH         0xA4
#define HID_GLOBAL_POP          0xB4

// HID 报告项标签 (Local)
#define HID_LOCAL_USAGE        0x04
#define HID_LOCAL_USAGE_MIN    0x14
#define HID_LOCAL_USAGE_MAX    0x24
#define HID_LOCAL_DESIGNATOR_INDEX 0x34
#define HID_LOCAL_DESIGNATOR_MIN  0x44
#define HID_LOCAL_DESIGNATOR_MAX  0x54
#define HID_LOCAL_STRING_INDEX    0x64
#define HID_LOCAL_STRING_MIN      0x74
#define HID_LOCAL_STRING_MAX      0x84
#define HID_LOCAL_DELIMITER       0xA4

// HID 用法页
#define HID_USAGE_PAGE_GENERIC_DESKTOP  0x01
#define HID_USAGE_PAGE_KEYBOARD         0x07
#define HID_USAGE_PAGE_BUTTON           0x09
#define HID_USAGE_PAGE_CONSUMER         0x0C

// HID 用法 (Generic Desktop)
#define HID_USAGE_POINTER    0x01
#define HID_USAGE_MOUSE     0x02
#define HID_USAGE_JOYSTICK  0x04
#define HID_USAGE_GAMEPAD   0x05
#define HID_USAGE_KEYBOARD  0x06
#define HID_USAGE_X        0x30
#define HID_USAGE_Y        0x31
#define HID_USAGE_WHEEL    0x38

// 解析报告描述符
static int hid_parse_report_descriptor(hid_device_t* hid, u8* desc, u32 length) {
    u32 pos = 0;
    u32 report_size = 0;
    u8 report_id = 0;
    
    while (pos < length) {
        u8 prefix = desc[pos];
        u8 size = prefix & 0x03;
        u8 type = (prefix >> 2) & 0x03;
        u8 tag = (prefix >> 4) & 0x0F;
        
        u32 value = 0;
        for (u8 i = 0; i < size && (pos + 1 + i) < length; i++) {
            value |= (desc[pos + 1 + i] << (8 * i));
        }
        
        USB_DEBUG("HID item: type=%d tag=%d size=%d value=%x\n", type, tag, size, value);
        
        if (type == HID_ITEM_TYPE_GLOBAL) {
            if (tag == HID_GLOBAL_REPORT_SIZE) {
                // report_size = value;
            } else if (tag == HID_GLOBAL_REPORT_ID) {
                report_id = value;
            } else if (tag == HID_GLOBAL_REPORT_COUNT) {
                report_size += value * ((report_id > 0) ? 1 : 0);
            }
        }
        
        pos += 1 + size;
    }
    
    hid->report_id = report_id;
    hid->report_size = report_size;
    
    USB_INFO("HID report: id=%d size=%d\n", report_id, report_size);
    
    return 0;
}

// 获取 HID 描述符
static int hid_get_descriptor(usb_device_t* dev, usb_hid_descriptor_t* desc) {
    return usb_control_transfer(dev->address, 0,
                               USB_REQUEST_TYPE_DEVICE_TO_HOST | USB_REQUEST_TYPE_CLASS | USB_REQUEST_TYPE_RECIPIENT_INTERFACE,
                               USB_REQUEST_GET_DESCRIPTOR,
                               (USB_DESCRIPTOR_TYPE_HID << 8) | 0,
                               0, desc, sizeof(usb_hid_descriptor_t), 1000);
}

// 获取报告描述符
static int hid_get_report_descriptor(usb_device_t* dev, u8* buffer, u16 length) {
    return usb_control_transfer(dev->address, 0,
                               USB_REQUEST_TYPE_DEVICE_TO_HOST | USB_REQUEST_TYPE_CLASS | USB_REQUEST_TYPE_RECIPIENT_INTERFACE,
                               USB_REQUEST_GET_DESCRIPTOR,
                               (USB_DESCRIPTOR_TYPE_REPORT << 8) | 0,
                               0, buffer, length, 1000);
}

// 获取报告
int hid_get_report(usb_device_t* dev, u8 report_id, void* buf, u32 len) {
    return usb_control_transfer(dev->address, 0,
                               USB_REQUEST_TYPE_DEVICE_TO_HOST | USB_REQUEST_TYPE_CLASS | USB_REQUEST_TYPE_RECIPIENT_INTERFACE,
                               USB_HID_REQUEST_GET_REPORT,
                               (report_id << 8) | 0x0300,  // Report Type: Input
                               0, buf, len, 1000);
}

// 设置报告
int hid_set_report(usb_device_t* dev, u8 report_id, void* buf, u32 len) {
    return usb_control_transfer(dev->address, 0,
                               USB_REQUEST_TYPE_HOST_TO_DEVICE | USB_REQUEST_TYPE_CLASS | USB_REQUEST_TYPE_RECIPIENT_INTERFACE,
                               USB_HID_REQUEST_SET_REPORT,
                               (report_id << 8) | 0x0300,  // Report Type: Output
                               0, buf, len, 1000);
}

// 获取空闲速率
static int hid_get_idle(usb_device_t* dev, u8 interface) {
    u8 idle;
    return usb_control_transfer(dev->address, 0,
                               USB_REQUEST_TYPE_DEVICE_TO_HOST | USB_REQUEST_TYPE_CLASS | USB_REQUEST_TYPE_RECIPIENT_INTERFACE,
                               USB_HID_REQUEST_GET_IDLE,
                               interface << 8, 0, &idle, 1, 1000);
}

// 设置空闲速率
static int hid_set_idle(usb_device_t* dev, u8 interface, u8 duration) {
    return usb_control_transfer(dev->address, 0,
                               USB_REQUEST_TYPE_HOST_TO_DEVICE | USB_REQUEST_TYPE_CLASS | USB_REQUEST_TYPE_RECIPIENT_INTERFACE,
                               USB_HID_REQUEST_SET_IDLE,
                               (duration << 8) | interface, 0, NULL, 0, 1000);
}

// 处理 HID 设备
int hid_handle_device(usb_device_t* dev) {
    if (dev == NULL) return -1;

    USB_INFO("Handling HID device: vid=%04x pid=%04x\n", dev->vendor, dev->product);

    // 分配 HID 设备
    hid_device_t* hid = kmalloc(sizeof(hid_device_t), KERNEL_TYPE);
    if (hid == NULL) {
        USB_ERROR("Failed to allocate HID device\n");
        return -1;
    }

    kmemset(hid, 0, sizeof(hid_device_t));
    hid->usb_dev = dev;
    hid->interface = 0;

    // 尝试获取 HID 描述符 (可选，Boot 设备不一定需要)
    usb_hid_descriptor_t hid_desc;
    if (hid_get_descriptor(dev, &hid_desc) == 0) {
        USB_INFO("HID: bcd_hid=%04x country=%d num_desc=%d\n",
                 hid_desc.bcd_hid, hid_desc.country_code, hid_desc.num_descriptors);

        // 尝试获取报告描述符
        u16 report_desc_len = hid_desc.descriptor_length;
        if (report_desc_len == 0) {
            report_desc_len = 256;  // 默认最大长度
        }

        hid->report_desc = kmalloc(report_desc_len, KERNEL_TYPE);
        if (hid->report_desc != NULL) {
            if (hid_get_report_descriptor(dev, hid->report_desc, report_desc_len) == 0) {
                // 解析报告描述符
                hid_parse_report_descriptor(hid, hid->report_desc, report_desc_len);
            } else {
                kfree(hid->report_desc);
                hid->report_desc = NULL;
            }
        }
    } else {
        USB_INFO("HID descriptor not available, using Boot protocol\n");
    }

    // 设置空闲速率 (中断端点轮询) - 可能失败，忽略错误
    hid_set_idle(dev, hid->interface, 10);  // 10ms

    // 添加到 HID 设备列表
    hid->next = hid_devices;
    hid_devices = hid;

    // 根据设备类型初始化 - 检查 subclass 和 protocol
    USB_INFO("HID device subclass=%d protocol=%d\n", dev->subclass, dev->protocol);
    USB_INFO("Before mouse: dev=%p addr=%d vid=%04x pid=%04x num_ep=%d\n",
             dev, dev->address, dev->vendor, dev->product, dev->num_endpoints);
    
    if (dev->subclass == 0x01) {
        // Boot 设备
        if (dev->protocol == 0x01) {
            // 键盘
            USB_INFO("HID Boot Keyboard detected\n");
        } else if (dev->protocol == 0x02) {
            // 鼠标
            USB_INFO("HID Boot Mouse detected\n");
            usb_mouse_connect(dev);
        }
    } else {
        USB_INFO("HID device is not Boot protocol, subclass=%d\n", dev->subclass);
    }

    return 0;
}

// 移除 HID 设备
void hid_remove_device(usb_device_t* dev) {
    hid_device_t* prev = NULL;
    hid_device_t* curr = hid_devices;
    
    while (curr != NULL) {
        if (curr->usb_dev == dev) {
            // 从列表移除
            if (prev == NULL) {
                hid_devices = curr->next;
            } else {
                prev->next = curr->next;
            }
            
            // 释放资源
            if (curr->report_desc != NULL) {
                kfree(curr->report_desc);
            }
            
            // 通知子类驱动
            usb_mouse_disconnect(dev);
            
            kfree(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }
}

// HID 初始化
void hid_init(void) {
    USB_INFO("USB HID driver initializing...\n");
    hid_devices = NULL;
    USB_INFO("USB HID driver initialized\n");
}

// 模块
module_t hid_module = {
    .name = "hid",
    .init = hid_init,
    .exit = NULL
};
