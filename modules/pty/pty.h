/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef PTY_H
#define PTY_H

#include "kernel/kernel.h"
#include "algorithm/buffer.h"

/* 环形缓冲容量：程序一次能写出几 KB（cat 大文件/ls 大目录），太小会短写；
 * 大缓冲也降低"主端还没来得及读"的概率。 */
#define PTY_BUFFER 4096

#define IOC_PTY_MAGIC 'p'

#define IOC_SLAVE _IOW(IOC_PTY_MAGIC, 1, int)
#define IOC_READ_OFFSET _IOW(IOC_PTY_MAGIC, 3, int)
#define IOC_WRITE_OFFSET _IOW(IOC_PTY_MAGIC, 4, int)
#define IOC_MASTER_READ_NOBLOCK _IOW(IOC_PTY_MAGIC, 5, int)
typedef struct pty{
    buffer_t *in;
    buffer_t* out;

    vnode_t* master;
    vnode_t* slave;

    u32 master_read_block;
    /* 本实例的从端编号（clone-on-open 时定下）：IOC_SLAVE/TIOCGPTN 都回它。
     * 入口节点的占位 pty_t 恒为 0，真正实例在 pty_clone_open 里赋值。 */
    int slave_no;
    /* 窗口大小（TIOCSWINSZ 存、TIOCGWINSZ 取）：主从端共享同一份 */
    u16 winsize[4]; /* ws_row, ws_col, ws_xpixel, ws_ypixel */
    /* 挂起状态：master 节点关闭后置位 ⇒ 从端 read 得 EOF(0)、write 得 EIO
     * （Linux 语义：master 没了从端就是死终端） */
    int dead;
    /* 当前打开着的从端 fd 数：归零后主端 read 回 -EIO（对端挂断），
     * hangup01 靠这个数 hangup 次数 */
    int slave_cnt;
}pty_t;

u32 pty_master_write(vnode_t *node, u32 offset, u32 size, u8 *buffer);
u32 pty_master_read(vnode_t *node, u32 offset, u32 size, u8 *buffer);

u32 pty_slave_read(vnode_t *node, u32 offset, u32 size, u8 *buffer);
u32 pty_slave_open(vnode_t *node, u32 mode);
u32 pty_slave_write(vnode_t *node, u32 offset, u32 size, u8 *buffer);


u32 pty_open(vnode_t *node, u32 mode);
u32 pty_close(vnode_t *node);
ssize_t pty_ioctl(vnode_t *node, u32 cmd, void *args);
int pty_slave_add(vnode_t *global_pts, struct pty *pty);



#endif
