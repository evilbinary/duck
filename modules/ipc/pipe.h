/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef PIPE_H
#define PIPE_H

#include "kernel/kernel.h"
#include "algorithm/buffer.h"
#include "queue/rw_queue.h"

#define PIPE_BUFFER 128
#define PIPE_WAIT_QUEUE_SIZE 20

/* ===================== ioctl 命令编码 =====================
 * 与 Linux asm-generic/ioctl.h 的 _IOC 位域一致：
 *   bit31..30 dir（1=写 2=读）| bit29..16 参数大小 | bit15..8 类型 | bit7..0 序号
 * 内核其它地方此前都是直接写字面量，这里给出编码宏，pipe 的命令一律用它拼。 */
#define IOCTL_IOC_DIRSHIFT 30
#define IOCTL_IOC_SIZESHIFT 16
#define IOCTL_IOC_TYPESHIFT 8
#define IOCTL_IOC_NRSHIFT 0

#define IOCTL_IOC_NONE 0
#define IOCTL_IOC_WRITE 1
#define IOCTL_IOC_READ 2

#define IOCTL_IOC(dir, type, nr, size)                     \
  (((u32)(dir) << IOCTL_IOC_DIRSHIFT) |                    \
   ((u32)(size) << IOCTL_IOC_SIZESHIFT) |                  \
   ((u32)(type) << IOCTL_IOC_TYPESHIFT) |                  \
   ((u32)(nr) << IOCTL_IOC_NRSHIFT))

#define IOCTL_IO(type, nr) IOCTL_IOC(IOCTL_IOC_NONE, type, nr, 0)
#define IOCTL_IOR(type, nr, size) IOCTL_IOC(IOCTL_IOC_READ, type, nr, sizeof(size))
#define IOCTL_IOW(type, nr, size) \
  IOCTL_IOC(IOCTL_IOC_WRITE, type, nr, sizeof(size))

/* 管道/终端通用命令（取值与 asm-generic/ioctls.h 相同） */
#define FIONREAD 0x541B /* 可读字节数 */
#define FIONBIO 0x5421  /* 设置/清除非阻塞 */
#define TCFLSH 0x540B   /* 清空未读数据 */

/* FIONREAD 的 _IO*('f', 3, int) 编码形式：部分运行库对同一 nr 用 _IOW 而不是
 * _IOR（历史实现只按 size/type/nr 匹配），两种方向都接受。 */
#define FIONREAD_IOC_READ IOCTL_IOR('f', 3, int)  /* 0x80046603 */
#define FIONREAD_IOC_WRITE IOCTL_IOW('f', 3, int) /* 0x40046603 */

typedef struct pipe{
    rw_queue_t* wait_queue;
    buffer_t *buffer;
    /* 引用计数：pipe_make 出一个节点被读写两端共 2 个 fd 引用，fd 最后
     * 一次关闭（fd_close → vclose，fork 复制的 fd 只减 use_count）才减 1，
     * 减到 0 才能真正释放缓冲区。 */
    u32 ref;
}pipe_t;

pipe_t* pipe_create(u32 size);

vnode_t* pipe_make(u32 size);

#endif