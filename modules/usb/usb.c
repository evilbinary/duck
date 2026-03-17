/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * USB Core Implementation
 ********************************************************************/
#include "usb.h"
#include "dev/devfs.h"

static usb_device_t* usb_devices = NULL;
static u8 usb_next_address = 1;
static int usb_initialized = 0;

// 获取设备描述符
static int usb_get_device_descriptor(usb_device_t* dev, usb_device_descriptor_t* desc) {
    return usb_control_transfer(dev->address, 0,
                               USB_REQUEST_TYPE_DEVICE_TO_HOST | USB_REQUEST_TYPE_RECIPIENT_DEVICE,
                               USB_REQUEST_GET_DESCRIPTOR,
                               (USB_DESCRIPTOR_TYPE_DEVICE << 8) | 0,
                               0, desc, sizeof(usb_device_descriptor_t), 1000);
}

// 设置地址
static int usb_set_address(usb_device_t* dev, u8 address) {
    int ret = usb_control_transfer(0, 0,
                                   USB_REQUEST_TYPE_HOST_TO_DEVICE | USB_REQUEST_TYPE_RECIPIENT_DEVICE,
                                   USB_REQUEST_SET_ADDRESS,
                                   address, 0, NULL, 0, 100);
    if (ret == 0) {
        dev->address = address;
    }
    return ret;
}

// 获取配置描述符
static int usb_get_configuration_descriptor(usb_device_t* dev, 
                                          usb_configuration_descriptor_t* desc) {
    return usb_control_transfer(dev->address, 0,
                               USB_REQUEST_TYPE_DEVICE_TO_HOST | USB_REQUEST_TYPE_RECIPIENT_DEVICE,
                               USB_REQUEST_GET_DESCRIPTOR,
                               (USB_DESCRIPTOR_TYPE_CONFIGURATION << 8) | 0,
                               0, desc, sizeof(usb_configuration_descriptor_t), 1000);
}

// 设置配置
static int usb_set_configuration(usb_device_t* dev, u8 config) {
    return usb_control_transfer(dev->address, 0,
                               USB_REQUEST_TYPE_HOST_TO_DEVICE | USB_REQUEST_TYPE_RECIPIENT_DEVICE,
                               USB_REQUEST_SET_CONFIGURATION,
                               config, 0, NULL, 0, 1000);
}

// 获取描述符长度
static u16 usb_get_descriptor_length(usb_device_t* dev, u8 type, u8 index) {
    usb_device_descriptor_t dev_desc;
    int ret = usb_get_device_descriptor(dev, &dev_desc);
    if (ret < 0) {
        return 0;
    }
    
    // 如果是设备描述符，直接返回
    if (type == USB_DESCRIPTOR_TYPE_DEVICE) {
        return dev_desc.length;
    }
    
    // 获取配置描述符来确定总长度
    usb_configuration_descriptor_t config_desc;
    ret = usb_get_configuration_descriptor(dev, &config_desc);
    if (ret < 0) {
        return 0;
    }
    
    return config_desc.total_length;
}

// 解析配置描述符
static int usb_parse_configuration(usb_device_t* dev, u8* buffer, u16 length) {
    u8* p = buffer;
    u8* end = buffer + length;
    
    // 跳过配置描述符
    if (p + sizeof(usb_configuration_descriptor_t) > end) {
        return -1;
    }
    usb_configuration_descriptor_t* config = (usb_configuration_descriptor_t*)p;
    p += config->length;
    
    dev->num_endpoints = 0;
    
    // 遍历所有接口和端点
    while (p < end) {
        u8 desc_length = p[0];
        u8 desc_type = p[1];
        
        if (desc_length == 0) break;
        
        if (p + desc_length > end) break;
        
        switch (desc_type) {
            case USB_DESCRIPTOR_TYPE_INTERFACE: {
                usb_interface_descriptor_t* intf = (usb_interface_descriptor_t*)p;
                dev->class = intf->interface_class;
                dev->subclass = intf->interface_subclass;
                dev->protocol = intf->interface_protocol;
                USB_INFO("Interface: class=%d subclass=%d protocol=%d\n",
                         intf->interface_class, intf->interface_subclass, intf->interface_protocol);
                break;
            }
            case USB_DESCRIPTOR_TYPE_ENDPOINT: {
                if (dev->num_endpoints < 16) {
                    kmemcpy(&dev->ep[dev->num_endpoints], p, 
                           sizeof(usb_endpoint_descriptor_t));
                    USB_INFO("Endpoint: addr=%02x attr=%02x maxpacket=%d\n",
                             dev->ep[dev->num_endpoints].address,
                             dev->ep[dev->num_endpoints].attributes,
                             dev->ep[dev->num_endpoints].max_packet_size);
                    dev->num_endpoints++;
                }
                break;
            }
            case USB_DESCRIPTOR_TYPE_HID: {
                USB_INFO("HID Descriptor found\n");
                break;
            }
        }
        
        p += desc_length;
    }
    
    return 0;
}

// 分配新设备
int usb_alloc_device(usb_device_t* dev) {
    if (dev == NULL) return -1;
    
    dev->address = 0;
    dev->next = usb_devices;
    usb_devices = dev;
    
    return 0;
}

// 释放设备
int usb_free_device(usb_device_t* dev) {
    usb_device_t* prev = NULL;
    usb_device_t* curr = usb_devices;
    
    while (curr != NULL) {
        if (curr == dev) {
            if (prev == NULL) {
                usb_devices = curr->next;
            } else {
                prev->next = curr->next;
            }
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }
    
    return -1;
}

// 查找设备
usb_device_t* usb_find_device(u8 address) {
    usb_device_t* dev = usb_devices;
    while (dev != NULL) {
        if (dev->address == address) {
            return dev;
        }
        dev = dev->next;
    }
    return NULL;
}

// 按 VID/PID 查找设备
usb_device_t* usb_find_device_by_vid_pid(u16 vendor, u16 product) {
    usb_device_t* dev = usb_devices;
    while (dev != NULL) {
        if (dev->vendor == vendor && dev->product == product) {
            return dev;
        }
        dev = dev->next;
    }
    return NULL;
}

// 控制传输
int usb_control_transfer(u8 device_address, u8 endpoint,
                        u8 request_type, u8 request,
                        u16 value, u16 index, void* data, u16 len, u32 timeout) {
    urb_t urb;
    kmemset(&urb, 0, sizeof(urb));
    
    urb.device_address = device_address;
    urb.endpoint = endpoint & 0x7F;
    urb.request_type = request_type;
    urb.request = request;
    urb.value = value;
    urb.index = index;
    urb.transfer_buffer = (u8*)data;
    urb.transfer_length = len;
    
    return hcd_submit_urb(&urb);
}

// 中断传输
int usb_interrupt_transfer(usb_device_t* dev, u8 endpoint, void* data, u16 len) {
    if (dev == NULL) return -1;
    
    urb_t urb;
    kmemset(&urb, 0, sizeof(urb));
    
    urb.device_address = dev->address;
    urb.endpoint = (endpoint & 0x7F) | 0x80;  // IN endpoint
    urb.transfer_buffer = (u8*)data;
    urb.transfer_length = len;
    
    return hcd_submit_urb(&urb);
}

// 批量传输
int usb_bulk_transfer(usb_device_t* dev, u8 endpoint, void* data, u32 len) {
    if (dev == NULL) return -1;
    
    urb_t urb;
    kmemset(&urb, 0, sizeof(urb));
    
    urb.device_address = dev->address;
    urb.endpoint = endpoint;
    urb.transfer_buffer = (u8*)data;
    urb.transfer_length = len;
    
    return hcd_submit_urb(&urb);
}

// 新设备连接回调
void usb_device_connected(usb_device_t* dev) {
    USB_INFO("USB device connected: vid=%04x pid=%04x\n", dev->vendor, dev->product);
    
    // 设置地址
    u8 new_addr = usb_next_address++;
    if (usb_set_address(dev, new_addr) != 0) {
        USB_ERROR("Failed to set address for device\n");
        return;
    }
    
    // 获取设备描述符
    usb_device_descriptor_t dev_desc;
    if (usb_get_device_descriptor(dev, &dev_desc) != 0) {
        USB_ERROR("Failed to get device descriptor\n");
        return;
    }
    
    dev->vendor = dev_desc.id_vendor;
    dev->product = dev_desc.id_product;
    dev->class = dev_desc.device_class;
    dev->subclass = dev_desc.device_subclass;
    dev->protocol = dev_desc.device_protocol;
    
    USB_INFO("Device: class=%d subclass=%d protocol=%d\n",
             dev->class, dev->subclass, dev->protocol);
    
    // 获取配置
    usb_configuration_descriptor_t config_desc;
    if (usb_get_configuration_descriptor(dev, &config_desc) != 0) {
        USB_ERROR("Failed to get configuration descriptor\n");
        return;
    }
    
    // 分配足够大的缓冲区获取完整配置
    u8* config_buffer = kmalloc(config_desc.total_length, KERNEL_TYPE);
    if (config_buffer == NULL) {
        USB_ERROR("Failed to allocate config buffer\n");
        return;
    }
    
    // 获取完整配置描述符
    int ret = usb_control_transfer(dev->address, 0,
                                  USB_REQUEST_TYPE_DEVICE_TO_HOST | USB_REQUEST_TYPE_RECIPIENT_DEVICE,
                                  USB_REQUEST_GET_DESCRIPTOR,
                                  (USB_DESCRIPTOR_TYPE_CONFIGURATION << 8) | 0,
                                  0, config_buffer, config_desc.total_length, 1000);
    
    if (ret >= 0) {
        usb_parse_configuration(dev, config_buffer, config_desc.total_length);
    }
    
    kfree(config_buffer);
    
    // 设置配置
    usb_set_configuration(dev, 1);
    
    // 根据设备类型调用对应驱动
    switch (dev->class) {
        case USB_CLASS_HID:
            USB_INFO("HID device detected\n");
            // hid_handle_device(dev);
            break;
        case USB_CLASS_HUB:
            USB_INFO("Hub device detected\n");
            // hub_probe(dev);
            break;
        case USB_CLASS_MASS_STORAGE:
            USB_INFO("Mass storage device detected\n");
            break;
        default:
            USB_INFO("Unknown device class: %d\n", dev->class);
            break;
    }
}

// 设备断开回调
void usb_device_disconnected(usb_device_t* dev) {
    USB_INFO("USB device disconnected: address=%d\n", dev->address);
    usb_free_device(dev);
}

// 初始化
void usb_init(void) {
    if (usb_initialized) {
        return;
    }

    USB_INFO("USB core initializing...\n");

    usb_devices = NULL;
    usb_next_address = 1;

    // 初始化 HCD
    if (hcd_init() != 0) {
        USB_ERROR("Failed to initialize HCD\n");
        return;
    }

    // 注册设备
    device_t* dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
    dev->name = "usb";
    dev->id = 0;
    dev->type = DEVICE_TYPE_CHAR;
    device_add(dev);

    usb_initialized = 1;
    USB_INFO("USB core initialized\n");
}

// 退出
void usb_exit(void) {
    if (!usb_initialized) {
        return;
    }
    
    USB_INFO("USB core exiting...\n");
    
    hcd_shutdown();
    
    // 释放所有设备
    usb_device_t* dev = usb_devices;
    while (dev != NULL) {
        usb_device_t* next = dev->next;
        kfree(dev);
        dev = next;
    }
    usb_devices = NULL;
    
    usb_initialized = 0;
    USB_INFO("USB core exited\n");
}

// 模块
module_t usb_module = {
    .name = "usb",
    .init = usb_init,
    .exit = usb_exit
};
