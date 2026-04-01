/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * X Window System - Module Entry
 ********************************************************************/
#include "xwin.h"
#include "kernel/module.h"
#include "dev/devfs.h"

// ========== 设备接口 ==========

static xdisplay_t xwin_display;
static xwin_device_t xwin_device;

size_t xwin_dev_read(device_t* dev, void* buf, size_t len) {
    return 0;
}

size_t xwin_dev_write(device_t* dev, const void* buf, size_t len) {
    return 0;
}

size_t xwin_dev_ioctl(device_t* dev, u32 cmd, void* args) {
    xwin_device_t* xdev = dev->data;
    if (xdev == NULL || xdev->display == NULL) {
        return -1;
    }
    
    xdisplay_t* disp = xdev->display;
    
    switch (cmd) {
        case 0:  // 获取显示信息
            if (args != NULL) {
                xdisplay_t* info = (xdisplay_t*)args;
                info->vga = disp->vga;
                info->window_count = disp->window_count;
            }
            break;
            
        case 1:  // 创建窗口
            // TODO: 从用户空间创建窗口
            break;
            
        case 2:  // 销毁窗口
            // TODO: 从用户空间销毁窗口
            break;
            
        case 3:  // 处理事件
            xwin_process_events(disp);
            break;
            
        case 4:  // 渲染
            xwin_render(disp);
            break;
            
        default:
            break;
    }
    
    return 0;
}

// ========== 模块初始化 ==========

int xwin_module_init(void) {
    log_info("xwin module init\n");
    
    // 查找VGA设备
    device_t* vga_dev = device_find(DEVICE_VGA);
    if (vga_dev == NULL) {
        vga_dev = device_find(DEVICE_VGA_QEMU);
    }
    if (vga_dev == NULL) {
        vga_dev = device_find(DEVICE_LCD);
    }
    
    if (vga_dev == NULL) {
        log_error("xwin: no display device found\n");
        return -1;
    }
    
    // 获取VGA设备信息
    vga_device_t* vga = vga_dev->data;
    if (vga == NULL) {
        log_error("xwin: vga device data is null\n");
        return -1;
    }
    
    // 初始化X Window系统
    int ret = xwin_init(&xwin_display, vga);
    if (ret != 0) {
        log_error("xwin: init failed\n");
        return -1;
    }

    xwin_register_syscall();
    
    // 初始化窗口管理器
    // xwm_init(&xwin_display);
    
    // 创建设备节点
    xwin_device.display = &xwin_display;
    
    device_t* dev = kmalloc(sizeof(device_t), KERNEL_TYPE);
    dev->name = "xwin";
    dev->id = DEVICE_VGA + 100;  // 分配一个唯一的设备ID
    dev->type = DEVICE_TYPE_VIRTUAL;
    dev->read = xwin_dev_read;
    dev->write = xwin_dev_write;
    dev->ioctl = xwin_dev_ioctl;
    dev->data = &xwin_device;
    device_add(dev);
    
    // 挂载到/dev/xwin
    vnode_t* xwin_node = vfs_create_node("xwin", V_FILE);
    vfs_mount(NULL, "/dev", xwin_node);
    xwin_node->device = dev;
    xwin_node->op = &device_operator;
    
    log_info("xwin: module loaded (%dx%d)\n", vga->width, vga->height);
    
    return 0;
}

void xwin_module_exit(void) {
    log_info("xwin module exit\n");
    xwin_exit(&xwin_display);
}

module_t xwin_module = {
    .name = "xwin",
    .init = xwin_module_init,
    .exit = xwin_module_exit
};
