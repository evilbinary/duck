/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * USB Hub Driver
 ********************************************************************/
#include "usb.h"

// Hub 描述符
typedef struct hub_descriptor {
    u8  length;
    u8  type;
    u8  num_ports;
    u16 characteristics;
    u8  power_good_delay;
    u8  max_power;
    u8  device_removable[32];
    u8  port_power_ctrl_mask[32];
} hub_descriptor_t;

// Hub 特征
#define HUB_CHAR_LPSM      (0x3 << 0)    // 电源管理模式
#define HUB_CHAR_COMPOUND  (1 << 2)      // 复合设备
#define HUB_CHAR_OCPM      (1 << 3)      // 过流保护模式
#define HUB_CHAR_TTTT      (1 << 5)      // 端口指示灯
#define HUB_CHAR_PORTIND   (1 << 6)      // 端口指示

// Hub 请求
#define HUB_REQUEST_GET_STATUS      0x00
#define HUB_REQUEST_CLEAR_FEATURE   0x01
#define HUB_REQUEST_GET_BUSY        0x02
#define HUB_REQUEST_SET_FEATURE     0x03
#define HUB_REQUEST_GET_DESCRIPTOR  0x06
#define HUB_REQUEST_SET_DESCRIPTOR  0x07
#define HUB_REQUEST_CLEAR_TT_BUFFER 0x08
#define HUB_REQUEST_RESET_TT        0x09
#define HUB_REQUEST_GET_TT_STATE    0x0A
#define HUB_REQUEST_STOP_TT         0x0B
#define HUB_REQUEST_SET_HUB_DEPTH    0x0C
#define HUB_REQUEST_GET_PORT_STATUS  0x0D
#define HUB_REQUEST_CLEAR_PORT_FEATURE  0x0E
#define HUB_REQUEST_GET_PORT_DESCRIPTOR 0x0F
#define HUB_REQUEST_SET_PORT_FEATURE    0x10

// Hub 特征
#define HUB_FEATURE_C_HUB_LOCAL_POWER   0x00
#define HUB_FEATURE_C_HUB_OVER_CURRENT   0x01
#define HUB_FEATURE_PORT_CONNECTION      0x00
#define HUB_FEATURE_PORT_ENABLE          0x01
#define HUB_FEATURE_PORT_SUSPEND         0x02
#define HUB_FEATURE_PORT_OVER_CURRENT     0x03
#define HUB_FEATURE_PORT_RESET           0x04
#define HUB_FEATURE_PORT_LINK_STATE      0x05
#define HUB_FEATURE_PORT_POWER           0x08
#define HUB_FEATURE_PORT_LOW_SPEED       0x09
#define HUB_FEATURE_C_PORT_CONNECTION    0x10
#define HUB_FEATURE_C_PORT_ENABLE        0x11
#define HUB_FEATURE_C_PORT_SUSPEND       0x12
#define HUB_FEATURE_C_PORT_OVER_CURRENT  0x13
#define HUB_FEATURE_C_PORT_RESET         0x14
#define HUB_FEATURE_PORT_TEST            0x15
#define HUB_FEATURE_PORT_INDICATOR       0x16

static usb_device_t* hubs = NULL;

// 获取 Hub 描述符
static int hub_get_descriptor(usb_device_t* hub, hub_descriptor_t* desc) {
    return usb_control_transfer(hub->address, 0,
                                USB_REQUEST_TYPE_DEVICE_TO_HOST | USB_REQUEST_TYPE_CLASS | USB_REQUEST_TYPE_RECIPIENT_DEVICE,
                                HUB_REQUEST_GET_DESCRIPTOR,
                                (USB_DESCRIPTOR_TYPE_HUB << 8) | 0,
                                0, desc, sizeof(hub_descriptor_t), 1000);
}

// 获取端口状态
static int hub_get_port_status(usb_device_t* hub, u8 port, u32* status) {
    return usb_control_transfer(hub->address, 0,
                               USB_REQUEST_TYPE_DEVICE_TO_HOST | USB_REQUEST_TYPE_CLASS | USB_REQUEST_TYPE_RECIPIENT_ENDPOINT,
                               HUB_REQUEST_GET_PORT_STATUS,
                               port, 0, status, 4, 1000);
}

// 设置端口特征
static int hub_set_port_feature(usb_device_t* hub, u8 port, u8 feature) {
    return usb_control_transfer(hub->address, 0,
                               USB_REQUEST_TYPE_HOST_TO_DEVICE | USB_REQUEST_TYPE_CLASS | USB_REQUEST_TYPE_RECIPIENT_ENDPOINT,
                               HUB_REQUEST_SET_PORT_FEATURE,
                               feature, port, NULL, 0, 1000);
}

// 清除端口特征
static int hub_clear_port_feature(usb_device_t* hub, u8 port, u8 feature) {
    return usb_control_transfer(hub->address, 0,
                               USB_REQUEST_TYPE_HOST_TO_DEVICE | USB_REQUEST_TYPE_CLASS | USB_REQUEST_TYPE_RECIPIENT_ENDPOINT,
                               HUB_REQUEST_CLEAR_PORT_FEATURE,
                               feature, port, NULL, 0, 1000);
}

// Hub 端口状态变化
void hub_port_status_changed(usb_device_t* hub, u8 port) {
    u32 status = 0;
    
    hub_get_port_status(hub, port, &status);
    
    USB_INFO("Hub port %d status: 0x%08x\n", port, status);
    
    if (status & USB_PORT_CONNECTION) {
        // 设备连接
        USB_INFO("Device connected to hub port %d\n", port);
        
        // 启用端口
        hub_set_port_feature(hub, port, HUB_FEATURE_PORT_POWER);
        
        // 等待设备稳定
        for (volatile int i = 0; i < 100000; i++);
        
        // 复位端口
        hub_set_port_feature(hub, port, HUB_FEATURE_PORT_RESET);
        
        // 等待复位完成
        for (volatile int i = 0; i < 500000; i++);
        
        // 检查端口状态
        hub_get_port_status(hub, port, &status);
        
        if (status & USB_PORT_ENABLE) {
            USB_INFO("Device enabled on hub port %d\n", port);
        }
    }
    
    if (status & USB_PORT_OVER_CURRENT) {
        USB_WARN("Over-current on hub port %d\n", port);
    }
}

// 探测 Hub
int hub_probe(usb_device_t* dev) {
    if (dev == NULL) return -1;
    
    USB_INFO("Probing USB hub: vid=%04x pid=%04x\n", dev->vendor, dev->product);
    
    // 获取 Hub 描述符
    hub_descriptor_t hub_desc;
    if (hub_get_descriptor(dev, &hub_desc) != 0) {
        USB_ERROR("Failed to get hub descriptor\n");
        return -1;
    }
    
    dev->maxchild = hub_desc.num_ports;
    USB_INFO("Hub has %d ports\n", hub_desc.num_ports);
    
    // 分配 Hub 设备结构
    // 注意: 实际实现需要在设备结构中保存更多 Hub 信息
    
    // 添加到 Hub 列表
    dev->next = (usb_device_t*)hubs;
    hubs = dev;
    
    // 为每个端口上电
    for (int i = 1; i <= hub_desc.num_ports; i++) {
        hub_set_port_feature(dev, i, HUB_FEATURE_PORT_POWER);
    }
    
    // 等待端口稳定
    for (volatile int i = 0; i < 100000; i++);
    
    // 检查端口连接状态
    for (int i = 1; i <= hub_desc.num_ports; i++) {
        u32 status = 0;
        hub_get_port_status(dev, i, &status);
        
        if (status & USB_PORT_CONNECTION) {
            USB_INFO("Hub port %d: device connected\n", i);
        }
    }
    
    return 0;
}

// 断开 Hub
int hub_disconnect(usb_device_t* dev) {
    if (dev == NULL) return -1;
    
    USB_INFO("Hub disconnected: vid=%04x pid=%04x\n", dev->vendor, dev->product);
    
    // 从 Hub 列表移除
    usb_device_t* prev = NULL;
    usb_device_t* curr = hubs;
    while (curr != NULL) {
        if (curr == dev) {
            if (prev == NULL) {
                hubs = (usb_device_t*)curr->next;
            } else {
                prev->next = curr->next;
            }
            break;
        }
        prev = curr;
        curr = (usb_device_t*)curr->next;
    }
    
    return 0;
}

// Hub 轮询 (需要周期性调用)
void hub_poll(void) {
    usb_device_t* hub = hubs;
    
    while (hub != NULL) {
        for (int i = 1; i <= hub->maxchild; i++) {
            u32 status = 0;
            hub_get_port_status(hub, i, &status);
            
            // 检查连接状态变化
            // 实际实现需要保存上一次状态进行比较
            
            if (status & USB_PORT_CONNECTION) {
                hub_port_status_changed(hub, i);
            }
        }
        
        hub = (usb_device_t*)hub->next;
    }
}

// Hub 初始化
void hub_init(void) {
    USB_INFO("USB hub driver initializing...\n");
    hubs = NULL;
    USB_INFO("USB hub driver initialized\n");
}

// 模块
module_t hub_module = {
    .name = "hub",
    .init = hub_init,
    .exit = NULL
};
