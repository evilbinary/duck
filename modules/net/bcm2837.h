/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef BCM2837_NET_H
#define BCM2837_NET_H

#include "types.h"

// BCM2837 USB Host Controller base address
#define USB_BASE            0x3F980000
#define USB_HOST_BASE       (USB_BASE + 0x0000)
#define USB_POWER_BASE      (USB_BASE + 0x0E00)
#define USB_DEV_BASE        (USB_BASE + 0x1000)
#define USB_OTG_BASE        (USB_BASE + 0x1000)
#define USB_PHY0_BASE       (USB_BASE + 0x2000)

// SMSC LAN9512/LAN9514 USB Ethernet Controller IDs
#define SMSC_VENDOR_ID      0x0424
#define LAN9512_PRODUCT_ID  0xEC00
#define LAN9514_PRODUCT_ID  0xEC00

// Network device state
typedef struct bcm2837_net {
    u32 usb_base;
    u32 mac_address[2];
    u32 rx_buffer[2048];
    u32 tx_buffer[2048];
    u8 is_initialized;
    u8 link_status;
    u8 speed;  // 10, 100 Mbps
    u8 duplex; // 0=half, 1=full
} bcm2837_net_t;

// Function declarations (统一接口，与 e1000.c 一致)
int net_init_device(device_t* dev);
size_t net_read(device_t* dev, void* buf, size_t len);
size_t net_write(device_t* dev, const void* buf, size_t len);
int net_ioctl(device_t* dev, u32 cmd, void* args);

#endif  // BCM2837_NET_H
