/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * DWC2 USB Host Controller Driver (for BCM2837/raspi3)
 ********************************************************************/
#include "usb.h"
#include "pic/pic.h"

// DWC2 基地址 - raspi3
#define DWC2_BASE        0x3F980000

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
#define DWC2_HPRT_PRTCTL_MASK (0xF << 8)
#define DWC2_HPRT_PRTPWR   (1 << 12)
#define DWC2_HPRT_L1STS   (1 << 13)

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

// 读取寄存器
static u32 dwc2_read(u32 offset) {
    return dwc2_read32(DWC2_BASE + offset);
}

// 写入寄存器
static void dwc2_write(u32 offset, u32 value) {
    dwc2_write32(DWC2_BASE + offset, value);
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

    // TODO: 在ARMv8-A上需要正确映射USB外设地址
    // 当前阶段只打印信息，不实际访问硬件
    USB_INFO("DWC2: USB peripheral at physical address 0x%08x\n", DWC2_BASE);
    USB_INFO("DWC2: NOTE - hardware access pending implementation\n");

    return 0;
#if 0
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

    // 刷新 FIFO
    dwc2_flush_all_fifos();

    // 配置 AHB
    dwc2_write(DWC2_GAHBCFG, DWC2_GAHBCFG_GINT);

    // 使能所有中断
    u32 gintmsk = 0;
    gintmsk |= DWC2_GINTSTS_PRT;     // 端口中断
    gintmsk |= DWC2_GINTSTS_HCINT;   // 主机通道中断
    gintmsk |= DWC2_GINTSTS_PTxFEmp; // TX FIFO 空
    gintmsk |= DWC2_GINTSTS_HChHltd; // 通道停止
    dwc2_write(DWC2_GINTMSK, gintmsk);

    USB_INFO("DWC2: core initialized\n");
    return 0;
#endif
}

// 初始化主机模式
static int dwc2_host_init(void) {
    USB_INFO("DWC2: initializing host mode...\n");

    // TODO: 在ARMv8-A上需要正确映射USB外设地址
    // 当前阶段只打印信息，不实际访问硬件
    USB_INFO("DWC2: NOTE - hardware host mode pending implementation\n");

    // 初始化通道数组
    for (int i = 0; i < 16; i++) {
        channels[i].in_use = 0;
        channels[i].urb = NULL;
    }

    USB_INFO("DWC2: host mode initialized\n");
    return 0;
#if 0
    // 配置主机
    USB_INFO("DWC2: host mode initialized\n");
    return 0;
#endif
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

// 启动传输
static int dwc2_start_transfer(urb_t* urb, int ch_num) {
    dwc2_channel_t* ch = &channels[ch_num];
    ch->urb = urb;
    
    // 配置传输大小
    u32 hctsiz = 0;
    hctsiz |= (urb->transfer_length & 0x7FFFF);
    hctsiz |= ((urb->index & 0xFF) << 19);  // PID
    hctsiz |= ((urb->transfer_length & 0x1FF) << 29);  // 包计数
    
    dwc2_write(DWC2_HCTSIZ(ch_num), hctsiz);
    
    // 配置通道
    dwc2_config_channel(ch_num);
    
    // 启动传输
    u32 hcchar = dwc2_read(DWC2_HCCHAR(ch_num));
    hcchar |= DWC2_HCCHAR_CHENA;
    dwc2_write(DWC2_HCCHAR(ch_num), hcchar);
    
    return 0;
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
    
    u8 ep_dir = (urb->endpoint & 0x80) ? 1 : 0;
    u8 ep_num = urb->endpoint & 0x0F;
    u8 device_addr = urb->device_address;
    
    // 确定端点类型
    u8 ep_type = USB_ENDPOINT_CONTROL;
    if (urb->request_type != 0) {
        // 控制传输使用控制端点
    } else if (urb->endpoint != 0) {
        // 其他传输类型
        ep_type = USB_ENDPOINT_BULK;
    }
    
    // 分配通道
    int ch_num = dwc2_alloc_channel(ep_num, ep_dir, USB_SPEED_FULL, 64, device_addr, ep_type);
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
    
    return 0;
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

// HCD 操作
static hcd_ops_t dwc2_hcd_ops = {
    .init = dwc2_init,
    .submit_urb = dwc2_submit_urb,
    .get_frame_number = dwc2_get_frame_number,
    .shutdown = dwc2_shutdown,
};

// 模块初始化
void dwc2_module_init(void) {
    hcd_register_ops(&dwc2_hcd_ops);
}

module_t dwc2_module = {
    .name = "dwc2",
    .init = dwc2_module_init,
    .exit = dwc2_shutdown
};
