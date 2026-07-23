/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * USB Host Controller Driver Interface
 ********************************************************************/
#include "usb.h"

static hcd_ops_t* hcd_ops = NULL;
static int hcd_initialized = 0;

// 注册 HCD 操作
void hcd_register_ops(hcd_ops_t* ops) {
    hcd_ops = ops;
}

// HCD 初始化
int hcd_init(void) {
    if (hcd_initialized) {
        return 0;
    }

    if (hcd_ops == NULL) {
        USB_ERROR("No HCD ops registered\n");
        return -1;
    }

    // 先设置初始化标志，这样在 HCD init 中调用 usb_device_connected 时
    // hcd_submit_urb 就不会直接返回 -1
    hcd_initialized = 1;

    if (hcd_ops->init != NULL) {
        hcd_ops->init();
    }

    USB_INFO("HCD initialized\n");

    return 0;
}

// HCD 关闭
void hcd_shutdown(void) {
    if (!hcd_initialized) {
        return;
    }
    
    if (hcd_ops != NULL && hcd_ops->shutdown != NULL) {
        hcd_ops->shutdown();
    }
    
    hcd_initialized = 0;
    USB_INFO("HCD shutdown\n");
}

// 提交 URB
int hcd_submit_urb(urb_t* urb) {
    if (!hcd_initialized || hcd_ops == NULL) {
        USB_ERROR("hcd_submit_urb: not initialized (init=%d ops=%p)\n", hcd_initialized, hcd_ops);
        return -1;
    }
    
    if (hcd_ops->submit_urb == NULL) {
        USB_ERROR("hcd_submit_urb: no submit_urb callback\n");
        return -1;
    }
    
    return hcd_ops->submit_urb(urb);
}

// 取消 URB
int hcd_unlink_urb(urb_t* urb) {
    if (!hcd_initialized) {
        return -1;
    }
    
    // 默认实现：直接等待完成
    return 0;
}

// 获取帧号
int hcd_get_frame_number(void) {
    if (!hcd_initialized || hcd_ops == NULL) {
        return -1;
    }
    
    if (hcd_ops->get_frame_number == NULL) {
        return -1;
    }
    
    return hcd_ops->get_frame_number();
}
