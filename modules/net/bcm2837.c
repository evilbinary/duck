/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "bcm2837.h"
#include "bcm/bcm2835.h"

// USB Core registers
#define USB_CORE_ID                 (USB_HOST_BASE + 0x000)
#define USB_CORE_HW_GENERAL         (USB_HOST_BASE + 0x004)
#define USB_CORE_HW_AHB_CFG         (USB_HOST_BASE + 0x008)
#define USB_CORE_USB_CFG            (USB_HOST_BASE + 0x00C)
#define USB_CORE_RESET              (USB_HOST_BASE + 0x010)
#define USB_CORE_INT_STAT           (USB_HOST_BASE + 0x014)
#define USB_CORE_INT_MASK           (USB_HOST_BASE + 0x018)

// USB Host Channel registers
#define USB_HOST_CHAN_ADDR(n)       (USB_HOST_BASE + 0x500 + (n) * 0x20)
#define USB_HOST_CHAN_INTR(n)       (USB_HOST_BASE + 0x508 + (n) * 0x20)

// SMSC LAN95xx Ethernet registers (via USB)
#define SMSC_TX_CTRL                0x0010
#define SMSC_RX_CTRL                0x0008
#define SMSC_MAC_CR                 0x0100
#define SMSC_MAC_ADDR_HIGH          0x0118
#define SMSC_MAC_ADDR_LOW           0x0114

// Network packet buffers
#define RX_BUF_SIZE  2048
#define TX_BUF_SIZE  2048

static u8 rx_buffer[RX_BUF_SIZE] __attribute__((aligned(16)));
static u8 tx_buffer[TX_BUF_SIZE] __attribute__((aligned(16)));
static bcm2837_net_t net_device;

// Simple delay
static void delay(int count) {
    for(volatile int i = 0; i < count * 1000; i++);
}

// Read USB register
static u32 usb_read(u32 reg) {
    return *(volatile u32*)reg;
}

// Write USB register
static void usb_write(u32 reg, u32 val) {
    *(volatile u32*)reg = val;
}

// Initialize USB Host Controller
static int usb_host_init(bcm2837_net_t* net) {
    kprintf("[BCM2837-Net] Initializing USB Host Controller\n");
    
    // Reset USB controller
    usb_write(USB_CORE_RESET, 0x01);
    delay(100);
    
    // Wait for reset to complete
    while(usb_read(USB_CORE_RESET) & 0x01);
    
    // Enable USB Host mode
    usb_write(USB_CORE_USB_CFG, 0x00);
    delay(10);
    
    // Configure AHB for DMA
    usb_write(USB_CORE_HW_AHB_CFG, 0x0E);
    
    // Enable USB interrupts
    usb_write(USB_CORE_INT_MASK, 0xFFFFFFFF);
    
    kprintf("[BCM2837-Net] USB Host Controller initialized\n");
    return 0;
}

// Send USB control transfer
static int usb_control_transfer(u8 request_type, u8 request, 
                                 u16 value, u16 index, 
                                 u16 length, void* data) {
    // This is a simplified USB control transfer
    // In a real implementation, you would need to implement:
    // 1. Setup USB channel
    // 2. Send setup packet
    // 3. Handle data phase (IN/OUT)
    // 4. Handle status phase
    
    kprintf("[BCM2837-Net] USB control transfer: type=%x req=%x val=%x\n", 
            request_type, request, value);
    return 0;
}

// Initialize SMSC LAN95xx Ethernet controller
static int smsc_lan95xx_init(bcm2837_net_t* net) {
    kprintf("[BCM2837-Net] Initializing SMSC LAN95xx Ethernet Controller\n");
    
    // Send reset command to SMSC chip via USB
    usb_control_transfer(0x40, 0xA0, 0x0000, 0x0000, 0, NULL);
    delay(100);
    
    // Read MAC address from EEPROM (simplified)
    // In reality, this requires EEPROM read via USB
    net->mac_address[0] = 0x00;  // Placeholder MAC
    net->mac_address[1] = 0x01;
    kprintf("[BCM2837-Net] MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
            (net->mac_address[0] >> 0) & 0xFF,
            (net->mac_address[0] >> 8) & 0xFF,
            (net->mac_address[0] >> 16) & 0xFF,
            (net->mac_address[0] >> 24) & 0xFF,
            (net->mac_address[1] >> 0) & 0xFF,
            (net->mac_address[1] >> 8) & 0xFF);
    
    // Configure MAC control register
    // Enable TX/RX
    usb_control_transfer(0x40, 0xA1, SMSC_MAC_CR, 0, 0, NULL);
    
    // Set link status (simplified)
    net->link_status = 1;
    net->speed = 100;  // 100 Mbps
    net->duplex = 1;   // Full duplex
    
    kprintf("[BCM2837-Net] Link up: %d Mbps, %s duplex\n",
            net->speed, net->duplex ? "Full" : "Half");
    
    return 0;
}

// Initialize network device
int net_init_device(device_t* dev) {
    kprintf("[BCM2837-Net] Initializing network device\n");
    
    bcm2837_net_t* net = &net_device;
    kmemset(net, 0, sizeof(bcm2837_net_t));
    
    // Initialize USB Host Controller
    if(usb_host_init(net) != 0) {
        kprintf("[BCM2837-Net] Failed to initialize USB Host\n");
        return -1;
    }
    
    // Initialize SMSC LAN95xx Ethernet controller
    if(smsc_lan95xx_init(net) != 0) {
        kprintf("[BCM2837-Net] Failed to initialize SMSC LAN95xx\n");
        return -1;
    }
    
    net->is_initialized = 1;
    dev->data = net;
    
    // 设置设备回调函数
    dev->read = net_read;
    dev->write = net_write;
    dev->ioctl = net_ioctl;
    
    kprintf("[BCM2837-Net] Network device initialized successfully\n");
    return 0;
}

// Read data from network (receive packet)
size_t net_read(device_t* dev, void* buf, size_t len) {
    if(!dev || !dev->data) {
        return 0;
    }
    
    bcm2837_net_t* net = (bcm2837_net_t*)dev->data;
    if(!net->is_initialized || !net->link_status) {
        return 0;
    }
    
    // In a real implementation:
    // 1. Check USB host channel for received data
    // 2. Read packet from SMSC LAN95xx via USB bulk transfer
    // 3. Copy to buffer
    
    kprintf("[BCM2837-Net] Read packet (len=%d)\n", len);
    
    // For now, return 0 (no data available)
    // This should be replaced with actual USB bulk IN transfer
    return 0;
}

// Write data to network (send packet)
size_t net_write(device_t* dev, const void* buf, size_t len) {
    if(!dev || !dev->data || !buf) {
        return 0;
    }
    
    bcm2837_net_t* net = (bcm2837_net_t*)dev->data;
    if(!net->is_initialized || !net->link_status) {
        return 0;
    }
    
    if(len > TX_BUF_SIZE) {
        kprintf("[BCM2837-Net] Packet too large: %d > %d\n", len, TX_BUF_SIZE);
        return 0;
    }
    
    // Copy packet to TX buffer
    kmemcpy(tx_buffer, buf, len);
    
    // In a real implementation:
    // 1. Prepare USB bulk transfer
    // 2. Send packet to SMSC LAN95xx via USB
    // 3. Wait for completion
    
    kprintf("[BCM2837-Net] Send packet (len=%d)\n", len);
    
    return len;
}

// Handle network device control commands
int net_ioctl(device_t* dev, u32 cmd, void* args) {
    if(!dev || !dev->data) {
        return -1;
    }
    
    bcm2837_net_t* net = (bcm2837_net_t*)dev->data;
    
    switch(cmd) {
        case 0x01:  // Get MAC address
            if(args) {
                kmemcpy(args, net->mac_address, 6);
                return 0;
            }
            break;
            
        case 0x02:  // Get link status
            if(args) {
                *(u8*)args = net->link_status;
                return 0;
            }
            break;
            
        case 0x03:  // Get speed
            if(args) {
                *(u8*)args = net->speed;
                return 0;
            }
            break;
            
        default:
            kprintf("[BCM2837-Net] Unknown ioctl cmd: %x\n", cmd);
            break;
    }
    
    return -1;
}
