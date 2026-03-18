/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * DWC2 USB Host Controller Driver (for BCM2837/raspi3)
 ********************************************************************/
#include "usb.h"
#include "pic/pic.h"
#include "kernel/page.h"
#include "platform/platform.h"

// DWC2 基地址 - raspi3 (物理地址)
#define DWC2_BASE_PHYS   0x3F980000
#define DWC2_BASE        DWC2_BASE_PHYS

// 核心全局寄存器
#define DWC2_GOTGCTL     (DWC2_BASE + 0x000)
#define DWC2_GOTGINT     (DWC2_BASE + 0x004)
#define DWC2_GAHBCFG     (DWC2_BASE + 0x008)
#define DWC2_GUSBCFG     (DWC2_BASE + 0x00C)
#define DWC2_GRSTCTL     (DWC2_BASE + 0x010)
#define DWC2_GINTSTS     (DWC2_BASE + 0x014)
#define DWC2_GINTMSK     (DWC2_BASE + 0x018)
#define DWC2_GRXSTSR     (DWC2_BASE + 0x01C)
#define DWC2_GRXSTSP     (DWC2_BASE + 0x020)
#define DWC2_GRXFSIZ     (DWC2_BASE + 0x024)
#define DWC2_GNPTXFSIZ   (DWC2_BASE + 0x028)
#define DWC2_GNPTXSTS    (DWC2_BASE + 0x02C)
#define DWC2_GI2CCTL     (DWC2_BASE + 0x030)
#define DWC2_GCCFG       (DWC2_BASE + 0x038)
#define DWC2_HPTXFSIZ    (DWC2_BASE + 0x100)
#define DWC2_DIEPTXF(n)  (DWC2_BASE + 0x104 + (n) * 0x04)

// 主机模式寄存器
#define DWC2_HCFG        (DWC2_BASE + 0x400)
#define DWC2_HFIR        (DWC2_BASE + 0x404)
#define DWC2_HPTXSTS     (DWC2_BASE + 0x408)
#define DWC2_HAINT      (DWC2_BASE + 0x40C)
#define DWC2_HAINTMSK   (DWC2_BASE + 0x410)

// 主机通道寄存器 (n = 0-15)
#define DWC2_HCCHAR(n)   (DWC2_BASE + 0x500 + (n) * 0x20)
#define DWC2_HCINT(n)    (DWC2_BASE + 0x508 + (n) * 0x20)
#define DWC2_HCINTMSK(n) (DWC2_BASE + 0x50C + (n) * 0x20)
#define DWC2_HCTSIZ(n)   (DWC2_BASE + 0x510 + (n) * 0x20)
#define DWC2_HCDMA(n)    (DWC2_BASE + 0x514 + (n) * 0x20)
#define DWC2_HCDMAB(n)   (DWC2_BASE + 0x514 + (n) * 0x20)

// 端口寄存器
#define DWC2_HPRT        (DWC2_BASE + 0x440)
#define DWC2_HPRT0       (DWC2_BASE + 0x440)

// GAHBCFG 位
#define DWC2_GAHBCFG_GINT    (1 << 0)
#define DWC2_GAHBCFG_TXFELVL (1 << 7)
#define DWC2_GAHBCCFG_PTXFELVL (1 << 8)

// GUSBCFG 位
#define DWC2_GUSBCFG_TOCAL   (0x3 << 10)
#define DWC2_GUSBCFG_PHYIF   (1 << 15)
#define DWC2_GUSBCFG_ULPI    (1 << 14)
#define DWC2_GUSBCFG_SRPCAP  (1 << 8)
#define DWC2_GUSBCFG_HNPCAP  (1 << 9)

// GRSTCTL 位
#define DWC2_GRSTCTL_CSRST   (1 << 0)
#define DWC2_GRSTCTL_PSRST   (1 << 1)
#define DWC2_GRSTCTL_FCRST   (1 << 2)
#define DWC2_GRSTCTL_RXFFLSH (1 << 4)
#define DWC2_GRSTCTL_TXFFLSH (1 << 5)

// GINTSTS 位
#define DWC2_GINTSTS_CMOD        (1 << 0)
#define DWC2_GINTSTS_MISED_P     (1 << 1)
#define DWC2_GINTSTS_INCOMPLP    (1 << 3)
#define DWC2_GINTSTS_INCOMPLSOUT (1 << 4)
#define DWC2_GINTSTS_OEPINT     (1 << 5)
#define DWC2_GINTSTS_IEPINT     (1 << 6)
#define DWC2_GINTSTS_go Hao     (1 << 7)
#define DWC2_GINTSTS_PTxFEmp    (1 << 8)
#define DWC2_GINTSTS_HChHltd    (1 << 9)
#define DWC2_GINTSTS_PRT        (1 << 11)
#define DWC2_GINTSTS_HCINT      (1 << 25)
#define DWC2_GINTSTS_PTXFEmp    (1 << 24)

// HCFG 位
#define DWC2_HCFG_FSLSPCS_MASK  (0x3 << 0)
#define DWC2_HCFG_FSLSPCS_48MHz (0x0 << 0)
#define DWC2_HCFG_FSLSPCS_6MHz  (0x2 << 0)
#define DWC2_HCFG_DESCDMA       (1 << 23)
#define DWC2_HCFG_PERCHTIMEn    (1 << 7)

// HPRT 位
#define DWC2_HPRT_PCSTS    (1 << 0)
#define DWC2_HPRT_PCDET   (1 << 1)
#define DWC2_HPRT_PENA    (1 << 2)
#define DWC2_HPRT_PENCHNG (1 << 3)
#define DWC2_HPRT_POCCHNG (1 << 4)
#define DWC2_HPRT_POCSTS  (1 << 5)
#define DWC2_HPRT_PRTRST  (1 << 8)
#define DWC2_HPRT_PRTCTL_MASK (0xF << 8)
#define DWC2_HPRT_PRTPWR   (1 << 12)
#define DWC2_HPRT_L1STS   (1 << 13)
#define DWC2_HPRT_PRTSPD_MASK   (0x3 << 17)
#define DWC2_HPRT_PRTSPD_SHIFT  17
#define DWC2_HPRT_PRTSPD_HS     (0 << 17)  // High Speed
#define DWC2_HPRT_PRTSPD_FS     (1 << 17)  // Full Speed
#define DWC2_HPRT_PRTSPD_LS     (2 << 17)  // Low Speed

// HCCHAR 位
#define DWC2_HCCHAR_MPSIZ_MASK  (0x7FF << 0)
#define DWC2_HCCHAR_EPNUM_MASK  (0xF << 11)
#define DWC2_HCCHAR_EPDIR       (1 << 15)
#define DWC2_HCCHAR_LSDEV       (1 << 17)
#define DWC2_HCCHAR_EPTYPE_MASK (0x3 << 18)
#define DWC2_HCCHAR_MULTICNT_MASK (0x3 << 20)
#define DWC2_HCCHAR_DEVADDR_MASK (0x7F << 22)
#define DWC2_HCCHAR_CHENA       (1 << 31)
#define DWC2_HCCHAR_CHDIS       (1 << 30)

// HCINT 位
#define DWC2_HCINT_XFERC    (1 << 0)
#define DWC2_HCINT_CHHLTD   (1 << 1)
#define DWC2_HCINT_AHBERR   (1 << 2)
#define DWC2_HCINT_STALL    (1 << 3)
#define DWC2_HCINT_NAK      (1 << 4)
#define DWC2_HCINT_ACK     (1 << 5)
#define DWC2_HCINT_NYET    (1 << 6)
#define DWC2_HCINT_TXERR   (1 << 7)
#define DWC2_HCINT_BBERR   (1 << 8)
#define DWC2_HCINT_FRMOR   (1 << 9)
#define DWC2_HCINT_DTERR   (1 << 10)

// 寄存器访问宏
#define dwc2_read32(addr)       (*(volatile u32*)(addr))
#define dwc2_write32(addr, val) (*(volatile u32*)(addr) = (val))

// 主机通道结构
typedef struct dwc2_channel {
    u8  in_use;
    u8  ep_num;
    u8  ep_dir;
    u8  speed;
    u16 mps;
    u8  device_addr;
    u8  ep_type;
    urb_t* urb;
} dwc2_channel_t;

static dwc2_channel_t channels[16];
static int dwc2_initialized = 0;

// 读取寄存器 (addr 已经是完整地址)
static u32 dwc2_read(u32 addr) {
    return dwc2_read32(addr);
}

// 写入寄存器 (addr 已经是完整地址)
static void dwc2_write(u32 addr, u32 value) {
    dwc2_write32(addr, value);
}

// 等待寄存器位清零
static int dwc2_wait_bit(u32 reg, u32 bit, int set, u32 timeout) {
    u32 count = timeout;
    while (count > 0) {
        u32 val = dwc2_read(reg);
        if (set) {
            if ((val & bit) != 0) return 0;
        } else {
            if ((val & bit) == 0) return 0;
        }
        count--;
    }
    return -1;
}

// 刷新 TX FIFO
static void dwc2_flush_txfifo(u32 num) {
    dwc2_write(DWC2_GRSTCTL, (num << 6) | DWC2_GRSTCTL_TXFFLSH);
    dwc2_wait_bit(DWC2_GRSTCTL, DWC2_GRSTCTL_TXFFLSH, 0, 1000);
}

// 刷新 RX FIFO
static void dwc2_flush_rxfifo(void) {
    dwc2_write(DWC2_GRSTCTL, DWC2_GRSTCTL_RXFFLSH);
    dwc2_wait_bit(DWC2_GRSTCTL, DWC2_GRSTCTL_RXFFLSH, 0, 1000);
}

// 刷新所有 FIFO
static void dwc2_flush_all_fifos(void) {
    dwc2_write(DWC2_GRSTCTL, DWC2_GRSTCTL_CSRST);
    // 等待软件复位完成
    dwc2_wait_bit(DWC2_GRSTCTL, DWC2_GRSTCTL_CSRST, 0, 1000);
}

// 初始化核心
static int dwc2_core_init(void) {
    USB_INFO("DWC2: initializing core...\n");

#if defined(RASPI3) || defined(RASPI2) || defined(ARMV8_A)
    // 读取 GUSBCFG
    u32 gusbcfg = dwc2_read(DWC2_GUSBCFG);

    // 配置 PHY 接口
    gusbcfg |= DWC2_GUSBCFG_PHYIF;  // 8-bit PHY
    gusbcfg &= ~DWC2_GUSBCFG_ULPI;  // 内置 PHY

    // 设置超时校准
    gusbcfg |= (0x3 << 10);

    dwc2_write(DWC2_GUSBCFG, gusbcfg);

    // 复位核心
    dwc2_write(DWC2_GRSTCTL, DWC2_GRSTCTL_CSRST);
    dwc2_wait_bit(DWC2_GRSTCTL, DWC2_GRSTCTL_CSRST, 0, 10000);

    // 等待时钟稳定
    for (volatile int i = 0; i < 10000; i++);

    // 配置 FIFO 大小
    // RX FIFO: 1024 words (4KB)
    dwc2_write(DWC2_GRXFSIZ, 1024);
    
    // 非周期 TX FIFO: 1024 words (4KB), 起始地址 1024
    dwc2_write(DWC2_GNPTXFSIZ, (1024 << 16) | 1024);

    // 刷新 FIFO
    dwc2_flush_all_fifos();

    // 配置 AHB - 使能全局中断
    dwc2_write(DWC2_GAHBCFG, DWC2_GAHBCFG_GINT);

    // 使能所有中断
    u32 gintmsk = 0;
    gintmsk |= DWC2_GINTSTS_PRT;     // 端口中断
    gintmsk |= DWC2_GINTSTS_HCINT;   // 主机通道中断
    gintmsk |= DWC2_GINTSTS_PTxFEmp; // TX FIFO 空
    gintmsk |= DWC2_GINTSTS_HChHltd; // 通道停止
    dwc2_write(DWC2_GINTMSK, gintmsk);

    USB_INFO("DWC2: core initialized\n");
#else
    USB_INFO("DWC2: NOTE - hardware access not supported on this platform\n");
#endif

    return 0;
}

// 初始化主机模式
static int dwc2_host_init(void) {
    USB_INFO("DWC2: initializing host mode...\n");

#if defined(RASPI3) || defined(RASPI2) || defined(ARMV8_A) || defined(V3S) || defined(T113_S3)
    // 配置主机模式
    u32 hcfg = dwc2_read(DWC2_HCFG);
    hcfg &= ~DWC2_HCFG_FSLSPCS_MASK;
    hcfg |= DWC2_HCFG_FSLSPCS_48MHz;  // 全速/高速使用 48MHz
    dwc2_write(DWC2_HCFG, hcfg);
    
    // 设置帧间隔 (全速: 48000, 高速: 125000)
    dwc2_write(DWC2_HFIR, 48000);
#endif

    // 初始化通道数组
    for (int i = 0; i < 16; i++) {
        channels[i].in_use = 0;
        channels[i].urb = NULL;
    }

    USB_INFO("DWC2: host mode initialized\n");
    return 0;
}

// 分配通道
static int dwc2_alloc_channel(u8 ep_num, u8 ep_dir, u8 speed, u16 mps, u8 device_addr, u8 ep_type) {
    for (int i = 0; i < 16; i++) {
        if (!channels[i].in_use) {
            channels[i].in_use = 1;
            channels[i].ep_num = ep_num;
            channels[i].ep_dir = ep_dir;
            channels[i].speed = speed;
            channels[i].mps = mps;
            channels[i].device_addr = device_addr;
            channels[i].ep_type = ep_type;
            return i;
        }
    }
    return -1;
}

// 释放通道
static void dwc2_free_channel(int ch_num) {
    if (ch_num >= 0 && ch_num < 16) {
        channels[ch_num].in_use = 0;
        channels[ch_num].urb = NULL;
    }
}

// 配置通道
static void dwc2_config_channel(int ch_num) {
    dwc2_channel_t* ch = &channels[ch_num];
    
    u32 hcchar = 0;
    hcchar |= (ch->mps & DWC2_HCCHAR_MPSIZ_MASK);
    hcchar |= ((ch->ep_num << 11) & DWC2_HCCHAR_EPNUM_MASK);
    if (ch->ep_dir) {
        hcchar |= DWC2_HCCHAR_EPDIR;
    }
    if (ch->speed == USB_SPEED_LOW) {
        hcchar |= DWC2_HCCHAR_LSDEV;
    }
    hcchar |= ((ch->ep_type << 18) & DWC2_HCCHAR_EPTYPE_MASK);
    hcchar |= ((ch->device_addr << 22) & DWC2_HCCHAR_DEVADDR_MASK);
    
    dwc2_write(DWC2_HCCHAR(ch_num), hcchar);
    
    // 使能所有通道中断
    u32 hcintmsk = 0;
    hcintmsk |= DWC2_HCINT_XFERC;
    hcintmsk |= DWC2_HCINT_CHHLTD;
    hcintmsk |= DWC2_HCINT_STALL;
    hcintmsk |= DWC2_HCINT_NAK;
    hcintmsk |= DWC2_HCINT_ACK;
    hcintmsk |= DWC2_HCINT_TXERR;
    hcintmsk |= DWC2_HCINT_FRMOR;
    dwc2_write(DWC2_HCINTMSK(ch_num), hcintmsk);
}

// HCTSIZ PID 值
#define DWC2_PID_DATA0  0
#define DWC2_PID_DATA1  (1 << 29)
#define DWC2_PID_DATA2  (2 << 29)
#define DWC2_PID_SETUP  (3 << 29)

// 等待通道传输完成
// 返回值: URB_OK, URB_NAK (无数据), URB_STALL, URB_ERROR
#define URB_NAK  (-2)  // NAK - 无数据可读

static int dwc2_wait_channel(int ch_num, u32* actual_len) {
    u32 timeout = 500;  // 中断传输应该快速响应
    u32 requested_len = channels[ch_num].mps;  // 从通道获取请求长度，后面会修正
    
    while (timeout > 0) {
        u32 hcint = dwc2_read(DWC2_HCINT(ch_num));
        
        // 通道停止 (CHHLTD) - 传输结束，检查结果
        if (hcint & DWC2_HCINT_CHHLTD) {
            dwc2_write(DWC2_HCINT(ch_num), hcint);  // 清除中断
            
            // 检查是否成功完成
            if (hcint & DWC2_HCINT_XFERC) {
                u32 hctsiz = dwc2_read(DWC2_HCTSIZ(ch_num));
                u32 remaining = hctsiz & 0x7FFFF;  // 剩余长度
                // 实际长度 = 请求长度 - 剩余长度 (由调用者计算)
                *actual_len = remaining;
                return URB_OK;
            }
            
            // 检查错误
            if (hcint & DWC2_HCINT_STALL) {
                return URB_STALL;
            }
            if (hcint & DWC2_HCINT_TXERR) {
                return URB_ERROR;
            }
            if (hcint & DWC2_HCINT_NAK) {
                return URB_NAK;  // NAK - 设备无数据
            }
            
            // CHHLTD 但没有其他标志 - 可能是成功
            // 对于零长度传输，可能只有 CHHLTD
            return URB_OK;
        }
        
        // 其他错误情况
        if (hcint & DWC2_HCINT_STALL) {
            dwc2_write(DWC2_HCINT(ch_num), hcint);
            return URB_STALL;
        }
        
        if (hcint & DWC2_HCINT_TXERR) {
            dwc2_write(DWC2_HCINT(ch_num), hcint);
            return URB_ERROR;
        }
        
        timeout--;
    }
    
    USB_ERROR("Transfer timeout\n");
    return URB_ERROR;
}

// 执行单次传输
static int dwc2_do_transfer(int ch_num, u8* buffer, u32 len, u8 ep_dir, u32 pid) {
    // 配置传输大小
    u32 hctsiz = 0;
    u32 pkt_count = (len + channels[ch_num].mps - 1) / channels[ch_num].mps;
    if (pkt_count == 0) pkt_count = 1;
    
    hctsiz |= (len & 0x7FFFF);            // 传输字节数 (bits 18:0)
    hctsiz |= ((pkt_count & 0x3FF) << 19); // 包计数 (bits 28:19, 10 bits)
    hctsiz |= pid;                         // PID (bits 30:29)
    
    dwc2_write(DWC2_HCTSIZ(ch_num), hctsiz);
    
    // 设置 DMA 地址
    if (buffer != NULL && len > 0) {
        dwc2_write(DWC2_HCDMA(ch_num), (u32)buffer);
    }
    
    // 配置通道
    dwc2_channel_t* ch = &channels[ch_num];
    ch->ep_dir = ep_dir;
    dwc2_config_channel(ch_num);
    
    // 启动传输
    u32 hcchar = dwc2_read(DWC2_HCCHAR(ch_num));
    if (ep_dir) hcchar |= DWC2_HCCHAR_EPDIR;
    hcchar |= DWC2_HCCHAR_CHENA;
    dwc2_write(DWC2_HCCHAR(ch_num), hcchar);
    
    USB_INFO("Transfer start: ch=%d len=%d dir=%d HCTSIZ=%08x HCCHAR=%08x\n", 
             ch_num, len, ep_dir, hctsiz, hcchar);
    
    // 等待完成
    u32 actual_len = 0;
    int ret = dwc2_wait_channel(ch_num, &actual_len);
    
    // 停止通道
    hcchar = dwc2_read(DWC2_HCCHAR(ch_num));
    hcchar |= DWC2_HCCHAR_CHDIS;
    hcchar &= ~DWC2_HCCHAR_CHENA;
    dwc2_write(DWC2_HCCHAR(ch_num), hcchar);
    
    // 打印中断状态
    u32 hcint = dwc2_read(DWC2_HCINT(ch_num));
    USB_INFO("Transfer end: ch=%d ret=%d HCINT=%08x\n", ch_num, ret, hcint);
    
    return ret;
}

// 控制传输 (三阶段: SETUP -> DATA -> STATUS)
static int dwc2_control_transfer(urb_t* urb) {
    u8 setup_packet[8];
    u8 device_addr = urb->device_address;
    
    // 构建 SETUP packet
    setup_packet[0] = urb->request_type;
    setup_packet[1] = urb->request;
    setup_packet[2] = urb->value & 0xFF;
    setup_packet[3] = (urb->value >> 8) & 0xFF;
    setup_packet[4] = urb->index & 0xFF;
    setup_packet[5] = (urb->index >> 8) & 0xFF;
    setup_packet[6] = urb->transfer_length & 0xFF;
    setup_packet[7] = (urb->transfer_length >> 8) & 0xFF;
    
    USB_INFO("Control transfer: addr=%d req=%02x val=%04x idx=%04x len=%d\n",
             device_addr, urb->request, urb->value, urb->index, urb->transfer_length);
    
    // 分配通道
    int ch_num = dwc2_alloc_channel(0, 0, USB_SPEED_FULL, 64, device_addr, USB_ENDPOINT_CONTROL);
    if (ch_num < 0) {
        USB_ERROR("Failed to allocate channel for control transfer\n");
        return -1;
    }
    
    int ret = URB_OK;
    u8 data_dir = (urb->request_type & 0x80) ? 1 : 0;
    
    // === 阶段 1: SETUP ===
    USB_INFO("Control SETUP phase\n");
    
    ret = dwc2_do_transfer(ch_num, setup_packet, 8, 0, DWC2_PID_SETUP);
    if (ret != URB_OK) {
        USB_ERROR("SETUP phase failed (%d)\n", ret);
        dwc2_free_channel(ch_num);
        return -1;
    }
    USB_INFO("Control SETUP phase OK\n");
    
    // === 阶段 2: DATA (如果有数据) ===
    if (urb->transfer_length > 0 && urb->transfer_buffer != NULL) {
        USB_INFO("Control DATA phase: dir=%d len=%d\n", data_dir, urb->transfer_length);
        
        ret = dwc2_do_transfer(ch_num, urb->transfer_buffer, urb->transfer_length, data_dir, DWC2_PID_DATA1);
        if (ret != URB_OK) {
            USB_ERROR("DATA phase failed (%d)\n", ret);
            dwc2_free_channel(ch_num);
            return -1;
        }
        
        // 计算实际传输长度
        u32 hctsiz = dwc2_read(DWC2_HCTSIZ(ch_num));
        u32 remaining = hctsiz & 0x7FFFF;
        urb->actual_length = urb->transfer_length - remaining;
        
        USB_INFO("Control DATA phase OK, actual=%d (remaining=%d)\n", urb->actual_length, remaining);
        
        // 打印接收到的数据（如果是 IN 传输）
        if (data_dir == 1 && urb->actual_length > 0) {
            USB_INFO("Data: %02x %02x %02x %02x %02x %02x %02x %02x ...\n",
                     urb->transfer_buffer[0], urb->transfer_buffer[1],
                     urb->transfer_buffer[2], urb->transfer_buffer[3],
                     urb->transfer_buffer[4], urb->transfer_buffer[5],
                     urb->transfer_buffer[6], urb->transfer_buffer[7]);
        }
    }
    
    // === 阶段 3: STATUS ===
    // STATUS 阶段方向与 DATA 阶段相反，DATA1 PID
    u8 status_dir = data_dir ? 0 : 1;  // 方向相反
    USB_INFO("Control STATUS phase: dir=%d\n", status_dir);
    
    ret = dwc2_do_transfer(ch_num, NULL, 0, status_dir, DWC2_PID_DATA1);
    if (ret != URB_OK) {
        USB_ERROR("STATUS phase failed (%d)\n", ret);
        dwc2_free_channel(ch_num);
        return -1;
    }
    
    dwc2_free_channel(ch_num);
    urb->status = URB_OK;
    USB_INFO("Control transfer complete\n");
    
    return urb->transfer_length;
}

// 启动传输 (非控制传输)
static int dwc2_start_transfer(urb_t* urb, int ch_num) {
    dwc2_channel_t* ch = &channels[ch_num];
    ch->urb = urb;
    urb->status = URB_PENDING;
    
    // 配置传输大小
    u32 hctsiz = 0;
    hctsiz |= (urb->transfer_length & 0x7FFFF);
    // 中断传输使用 DATA0, 批量传输使用 DATA1
    hctsiz |= (ch->ep_type == USB_ENDPOINT_INTERRUPT) ? DWC2_PID_DATA0 : DWC2_PID_DATA1;
    u32 pkt_count = (urb->transfer_length + ch->mps - 1) / ch->mps;
    if (pkt_count == 0) pkt_count = 1;
    hctsiz |= ((pkt_count & 0xFF) << 19);  // 包计数
    
    dwc2_write(DWC2_HCTSIZ(ch_num), hctsiz);
    
    // 配置通道
    dwc2_config_channel(ch_num);
    
    // 设置 DMA 地址 (如果有数据传输)
    if (urb->transfer_buffer != NULL && urb->transfer_length > 0) {
        dwc2_write(DWC2_HCDMA(ch_num), (u32)urb->transfer_buffer);
    }
    
    // 启动传输
    u32 hcchar = dwc2_read(DWC2_HCCHAR(ch_num));
    hcchar |= DWC2_HCCHAR_CHENA;
    dwc2_write(DWC2_HCCHAR(ch_num), hcchar);
    
    // 等待完成
    u32 actual_len = 0;
    int ret = dwc2_wait_channel(ch_num, &actual_len);
    
    // 停止通道
    hcchar = dwc2_read(DWC2_HCCHAR(ch_num));
    hcchar |= DWC2_HCCHAR_CHDIS;
    hcchar &= ~DWC2_HCCHAR_CHENA;
    dwc2_write(DWC2_HCCHAR(ch_num), hcchar);
    
    urb->status = ret;
    if (ret == URB_OK) {
        // actual_len 是剩余长度，需要计算实际传输长度
        urb->actual_length = urb->transfer_length - actual_len;
    } else if (ret == URB_NAK) {
        // NAK - 设备无数据，返回 0
        urb->status = URB_OK;
        urb->actual_length = 0;
    }
    
    // 释放通道
    dwc2_free_channel(ch_num);
    
    return (urb->status == URB_OK) ? urb->actual_length : -1;
}

// 处理通道中断
static void dwc2_handle_channel_intr(int ch_num) {
    dwc2_channel_t* ch = &channels[ch_num];
    u32 hcint = dwc2_read(DWC2_HCINT(ch_num));
    
    // 清除中断
    dwc2_write(DWC2_HCINT(ch_num), hcint);
    
    if (hcint & DWC2_HCINT_XFERC) {
        // 传输完成
        USB_DEBUG("Channel %d transfer complete\n", ch_num);
        if (ch->urb != NULL) {
            ch->urb->status = URB_OK;
            if (ch->urb->complete != NULL) {
                ch->urb->complete(ch->urb);
            }
        }
    }
    
    if (hcint & DWC2_HCINT_STALL) {
        USB_DEBUG("Channel %d STALL\n", ch_num);
        if (ch->urb != NULL) {
            ch->urb->status = URB_STALL;
        }
    }
    
    if (hcint & DWC2_HCINT_NAK) {
        USB_DEBUG("Channel %d NAK\n", ch_num);
    }
    
    if (hcint & DWC2_HCINT_TXERR) {
        USB_ERROR("Channel %d TX error\n", ch_num);
        if (ch->urb != NULL) {
            ch->urb->status = URB_ERROR;
        }
    }
    
    if (hcint & DWC2_HCINT_FRMOR) {
        USB_DEBUG("Channel %d frame overrun\n", ch_num);
    }
    
    // 释放通道
    dwc2_free_channel(ch_num);
}

// 处理端口中断
static void dwc2_handle_port_intr(void) {
    u32 hprt = dwc2_read(DWC2_HPRT0);
    
    if (hprt & DWC2_HPRT_PCDET) {
        // 设备连接检测
        USB_INFO("USB device connected\n");
        // TODO: 通知 USB 核心
    }
    
    if (hprt & DWC2_HPRT_PENCHNG) {
        // 端口使能状态变化
        if (hprt & DWC2_HPRT_PENA) {
            USB_INFO("USB port enabled\n");
        } else {
            USB_INFO("USB port disabled\n");
        }
    }
    
    if (hprt & DWC2_HPRT_POCCHNG) {
        // 过流变化
    }
    
    // 清除中断
    hprt &= ~0x3E;
    dwc2_write(DWC2_HPRT0, hprt);
}

// USB 中断处理
INTERRUPT_SERVICE
void dwc2_irq_handler(void) {
    interrupt_entering_code(ISR_USB, 0, 0);
    
    u32 gintsts = dwc2_read(DWC2_GINTSTS);
    
    if (gintsts & DWC2_GINTSTS_PRT) {
        dwc2_handle_port_intr();
    }
    
    if (gintsts & DWC2_GINTSTS_HCINT) {
        u32 haint = dwc2_read(DWC2_HAINT);
        for (int i = 0; i < 16; i++) {
            if (haint & (1 << i)) {
                dwc2_handle_channel_intr(i);
            }
        }
    }
    
    // 清除中断
    dwc2_write(DWC2_GINTSTS, gintsts);
    
    interrupt_exit();
}

// 提交 URB
static int dwc2_submit_urb(urb_t* urb) {
    if (urb == NULL) return -1;

    u8 ep_num = urb->endpoint & 0x0F;
    u8 device_addr = urb->device_address;

    // 判断是否是控制传输：端点0 或者有 request 字段
    // 控制传输需要发送 SETUP packet
    int is_control = (ep_num == 0);

    if (is_control) {
        // 控制传输需要三阶段处理
        return dwc2_control_transfer(urb);
    }

    // 其他传输类型 (中断/批量)
    u8 ep_dir = (urb->endpoint & 0x80) ? 1 : 0;
    u8 ep_type = USB_ENDPOINT_INTERRUPT;  // 假设中断传输
    u16 mps = urb->transfer_length > 0 ? urb->transfer_length : 8;  // 默认 8 字节

    // 从 USB 设备获取正确的 max_packet_size (如果可用)
    usb_device_t* usb_dev = usb_find_device(device_addr);
    if (usb_dev != NULL) {
        for (int i = 0; i < usb_dev->num_endpoints; i++) {
            if ((usb_dev->ep[i].address & 0x0F) == ep_num) {
                mps = usb_dev->ep[i].max_packet_size;
                USB_DEBUG("Using max_packet_size=%d for ep %d\n", mps, ep_num);
                break;
            }
        }
    }

    // 分配通道
    int ch_num = dwc2_alloc_channel(ep_num, ep_dir, USB_SPEED_FULL, mps, device_addr, ep_type);
    if (ch_num < 0) {
        USB_ERROR("Failed to allocate channel\n");
        return -1;
    }

    return dwc2_start_transfer(urb, ch_num);
}

// 获取帧号
static int dwc2_get_frame_number(void) {
    // 从 HFNUM 读取
    return 0;
}

// 前向声明
static void dwc2_detect_device(void);

// DWC2 初始化
int dwc2_init(void) {
    if (dwc2_initialized) {
        return 0;
    }
    
    USB_INFO("DWC2 USB controller initializing...\n");
    
    // 初始化核心
    if (dwc2_core_init() != 0) {
        return -1;
    }
    
    // 初始化主机模式
    if (dwc2_host_init() != 0) {
        return -1;
    }
    
    // 注册中断处理
    // interrupt_register(IRQ_USB, dwc2_irq_handler);
    
    dwc2_initialized = 1;
    USB_INFO("DWC2 USB controller initialized\n");
    
    // 检测端口上的设备
    dwc2_detect_device();
    
    return 0;
}

// 检测并枚举端口上的设备
static void dwc2_detect_device(void) {
#if defined(RASPI3) || defined(RASPI2) || defined(ARMV8_A) || defined(V3S) || defined(T113_S3)
    USB_INFO("DWC2: checking port status...\n");
    
    // 先启用端口电源
    u32 hprt = dwc2_read(DWC2_HPRT);
    hprt |= DWC2_HPRT_PRTPWR;
    dwc2_write(DWC2_HPRT, hprt);
    
    // 等待电源稳定和设备连接
    USB_INFO("DWC2: power on, waiting for device...\n");
    for (volatile int i = 0; i < 500000; i++);
    
    // 再次读取端口状态
    hprt = dwc2_read(DWC2_HPRT);
    USB_INFO("DWC2: HPRT=%08x\n", hprt);
    
    // 检查是否有设备连接
    if (hprt & DWC2_HPRT_PCSTS) {
        USB_INFO("DWC2: device connected, resetting port...\n");
        
        // 清除之前的连接检测中断
        if (hprt & DWC2_HPRT_PCDET) {
            dwc2_write(DWC2_HPRT, DWC2_HPRT_PCDET);
        }
        
        // 复位端口 - 写 1 到 PRTRST
        hprt = dwc2_read(DWC2_HPRT);
        hprt |= DWC2_HPRT_PRTRST;
        dwc2_write(DWC2_HPRT, hprt);
        
        // 等待复位完成 (至少 10ms，USB规范要求至少50ms)
        for (volatile int i = 0; i < 1000000; i++);
        
        // 清除复位 - 写 0 到 PRTRST
        hprt = dwc2_read(DWC2_HPRT);
        hprt &= ~DWC2_HPRT_PRTRST;
        dwc2_write(DWC2_HPRT, hprt);
        
        // 等待端口使能 (PENA = 1)
        USB_INFO("DWC2: waiting for port enable...\n");
        int timeout = 1000000;
        while (timeout > 0) {
            hprt = dwc2_read(DWC2_HPRT);
            if (hprt & DWC2_HPRT_PENA) {
                break;
            }
            timeout--;
        }
        
        if (timeout == 0) {
            USB_ERROR("DWC2: port enable timeout\n");
            return;
        }
        
        // 清除端口使能变化中断
        if (hprt & DWC2_HPRT_PENCHNG) {
            dwc2_write(DWC2_HPRT, DWC2_HPRT_PENCHNG);
        }
        
        // 获取设备速度
        u8 speed = USB_SPEED_FULL;
        u32 spd = (hprt & DWC2_HPRT_PRTSPD_MASK) >> DWC2_HPRT_PRTSPD_SHIFT;
        if (spd == 0) {
            speed = USB_SPEED_HIGH;
            USB_INFO("DWC2: High Speed device\n");
        } else if (spd == 1) {
            speed = USB_SPEED_FULL;
            USB_INFO("DWC2: Full Speed device\n");
        } else if (spd == 2) {
            speed = USB_SPEED_LOW;
            USB_INFO("DWC2: Low Speed device\n");
        }
        
        USB_INFO("DWC2: port enabled, HPRT=%08x\n", hprt);
        
        // 分配 USB 设备结构
        usb_device_t* dev = kmalloc(sizeof(usb_device_t), KERNEL_TYPE);
        if (dev != NULL) {
            kmemset(dev, 0, sizeof(usb_device_t));
            dev->address = 0;  // 默认地址
            dev->speed = speed;
            
            // 枚举设备
            usb_device_connected(dev);
        }
    } else {
        USB_INFO("DWC2: no device connected (HPRT=%08x)\n", hprt);
    }
#else
    USB_INFO("DWC2: port detection not supported on this platform\n");
#endif
}

// DWC2 关闭
void dwc2_shutdown(void) {
    if (!dwc2_initialized) {
        return;
    }
    
    USB_INFO("DWC2 USB controller shutting down...\n");
    
    // 禁用端口
    dwc2_write(DWC2_HPRT0, 0);
    
    // 刷新 FIFO
    dwc2_flush_all_fifos();
    
    dwc2_initialized = 0;
    USB_INFO("DWC2 USB controller shut down\n");
}


void usb_host_init(void) {
    dwc2_module_init();
}

// USB设备模式初始化 - 预留接口
void usb_device_init(void) {
    // no-op
}

// HCD 操作
static hcd_ops_t dwc2_hcd_ops = {
    .init = dwc2_init,
    .submit_urb = dwc2_submit_urb,
    .get_frame_number = dwc2_get_frame_number,
    .shutdown = dwc2_shutdown,
};

// 模块初始化
void dwc2_module_init(void) {
    // 映射 USB 外设地址 (ARMv8-A 需要在启用 MMU 后映射)
    page_map(DWC2_BASE_PHYS & ~0xfff, DWC2_BASE_PHYS & ~0xfff, PAGE_DEV);

    hcd_register_ops(&dwc2_hcd_ops);
}

module_t dwc2_module = {
    .name = "dwc2",
    .init = dwc2_module_init,
    .exit = dwc2_shutdown
};
