/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "pipe.h"

#include "kernel/kernel.h"
#include "kernel/thread.h"

/* 【设计：内核不阻塞，缓冲按需增长】
 * 本内核 schedule_switch() 丢弃内核调用链（线程被切走后从保存帧直接回用户
 * 态，切换点之后的 C 代码不再执行），"睡一觉接着读"写不出来；能用的只有
 * "唤醒方代办"那一套（waitpid 代收割）。pipe 不用这套：
 *   读：有数据就取（可短读），没有返回 0；
 *   写：有空间就放，满了就把缓冲翻倍（上限 PIPE_MAX_BUFFER），仍放不下才短写。
 * 这样"先用 select/poll 等就绪再读写"的常规用法就够了，也不会因为缓冲太小
 * 出现短写丢数据（Xorg 把整份 keymap 写进管道喂 xkbcomp 就是几十 KB）。
 * 端口计数（readers/writers）也不做：dup2/fork 复制的 fd 槽不一定都过
 * close，按端计数会把还没关的一端误判成关了（实测父进程关掉自己那份读端
 * 后，写端写进来的 keymap 全被静默丢弃）。 */

static void pipe_lock(pipe_t* p) { acquire(&p->lock); }

static void pipe_unlock(pipe_t* p) { release(&p->lock); }

pipe_t* pipe_create(u32 size) {
  pipe_t* pipe = kmalloc(sizeof(pipe_t), KERNEL_TYPE);
  if (pipe == NULL) {
    return NULL;
  }
  kmemset(pipe, 0, sizeof(pipe_t));
  pipe->size = (size > 0) ? size : (u32)PIPE_BUFFER;
  pipe->buf = kmalloc(pipe->size, KERNEL_TYPE);
  if (pipe->buf == NULL) {
    kfree(pipe);
    return NULL;
  }
  pipe->ref = 2; /* 读端、写端各一个 vnode */
  return pipe;
}

void pipe_destroy(pipe_t* pipe) {
  if (pipe == NULL) {
    return;
  }
  if (pipe->buf != NULL) {
    kfree(pipe->buf);
    pipe->buf = NULL;
  }
  kfree(pipe);
}

/* ---------------- 环形缓冲 ---------------- */

/* 扩容：把已有内容按序搬到新缓冲（head 归零）。调用方持锁。 */
static int pipe_grow(pipe_t* p) {
  u32 new_size;
  u8* nbuf;
  u32 i;
  u32 tail;
  if (p->size >= (u32)PIPE_MAX_BUFFER) {
    return -1;
  }
  new_size = p->size * 2;
  if (new_size > (u32)PIPE_MAX_BUFFER) {
    new_size = (u32)PIPE_MAX_BUFFER;
  }
  nbuf = kmalloc(new_size, KERNEL_TYPE);
  if (nbuf == NULL) {
    return -1;
  }
  tail = p->head;
  for (i = 0; i < p->count; i++) {
    nbuf[i] = p->buf[tail];
    if (++tail == p->size) {
      tail = 0;
    }
  }
  kfree(p->buf);
  p->buf = nbuf;
  p->size = new_size;
  p->head = 0;
  return 0;
}

static u32 pipe_take(pipe_t* p, u8* dst, u32 len) {
  u32 n = (len < p->count) ? len : p->count;
  u32 i;
  for (i = 0; i < n; i++) {
    if (dst != NULL) {
      dst[i] = p->buf[p->head];
    }
    if (++p->head == p->size) {
      p->head = 0;
    }
  }
  p->count -= n;
  return n;
}

static u32 pipe_append(pipe_t* p, const u8* src, u32 len) {
  u32 space = p->size - p->count;
  u32 n;
  u32 tail;
  u32 i;
  /* 空间不够且还能长：先扩容再放（避免短写丢数据） */
  if (space < len && p->size < (u32)PIPE_MAX_BUFFER) {
    while (p->size - p->count < len && p->size < (u32)PIPE_MAX_BUFFER) {
      if (pipe_grow(p) < 0) {
        break;
      }
    }
    space = p->size - p->count;
  }
  n = (len < space) ? len : space;
  tail = p->head + p->count;
  if (tail >= p->size) {
    tail -= p->size;
  }
  for (i = 0; i < n; i++) {
    p->buf[tail] = src[i];
    if (++tail == p->size) {
      tail = 0;
    }
  }
  p->count += n;
  return n;
}

/* ---------------- 读端 ---------------- */

static u32 pip_read(vnode_t* node, u32 offset, u32 size, u8* buffer) {
  pipe_t* p = (node != NULL) ? node->device : NULL;
  u32 n;
  if (p == NULL || buffer == NULL || size == 0) {
    return 0;
  }
  pipe_lock(p);
  n = pipe_take(p, buffer, size);
  pipe_unlock(p);
  {
    /* 【诊断·可删】 */
    static int r_dbg = 0;
    thread_t* c = thread_current();
    if (r_dbg++ < 40) {
      log_error("PIPR tid=%d want=%d got=%d left=%d\n", c != NULL ? c->id : -1,
                (int)size, (int)n, (int)p->count);
    }
  }
  return n;
}

/* ---------------- 写端 ---------------- */

static u32 pip_write(vnode_t* node, u32 offset, u32 size, u8* buffer) {
  pipe_t* p = (node != NULL) ? node->device : NULL;
  u32 n;
  if (p == NULL || buffer == NULL || size == 0) {
    return 0;
  }
  pipe_lock(p);
  n = pipe_append(p, buffer, size);
  pipe_unlock(p);
  {
    /* 【诊断·可删】 */
    static int w_dbg = 0;
    thread_t* c = thread_current();
    if (w_dbg++ < 40) {
      log_error("PIPW tid=%d size=%d put=%d cap=%d\n", c != NULL ? c->id : -1,
                (int)size, (int)n, (int)p->size);
    }
  }
  return n;
}

/* ---------------- ioctl ---------------- */

static u32 pip_ioctl(vnode_t* node, u32 cmd, void* args) {
  pipe_t* p = (node != NULL) ? node->device : NULL;
  u32 count = 0;
  if (p == NULL) {
    return (u32)-1;
  }
  pipe_lock(p);
  count = p->count;
  if (cmd == TCFLSH) {
    p->head = 0;
    p->count = 0;
  }
  pipe_unlock(p);
  switch (cmd) {
    case FIONREAD:
    case FIONREAD_IOC_READ:
    case FIONREAD_IOC_WRITE:
      if (args != NULL) {
        *(int*)args = (int)count;
      }
      return 0;
    case FIONBIO:
      return 0;
    case TCFLSH: /* 丢弃缓冲区里未读的数据 */
      return 0;
    default:
      log_debug("pipe ioctl unsupported cmd=%x\n", cmd);
      return 0;
  }
}

/* ---------------- 关闭 ---------------- */

/* 释放 vnode 自身：pipe 的 vnode 是临时对象（op 是静态表，
 * name 由 vfs_create_node 分配）。 */
static void pipe_node_free(vnode_t* node) {
  if (node == NULL) {
    return;
  }
  node->device = NULL;
  node->op = NULL;
  if (node->name != NULL) {
    kfree(node->name);
    node->name = NULL;
  }
  kfree(node);
}

static u32 pipe_close_end(vnode_t* node) {
  pipe_t* p = (node != NULL) ? node->device : NULL;
  if (p != NULL) {
    pipe_lock(p);
    if (p->ref > 0) {
      p->ref--;
    }
    pipe_unlock(p);
    if (p->ref == 0) {
      pipe_destroy(p);
    }
  }
  pipe_node_free(node);
  return 0;
}

/* ---------------- 节点表 ---------------- */

/* 读写两端各自的 op：read/write 只有自己那一半。 */
static voperator_t pipe_read_ops = {
    .read = pip_read,
    .close = pipe_close_end,
    .ioctl = pip_ioctl,
};

static voperator_t pipe_write_ops = {
    .write = pip_write,
    .close = pipe_close_end,
    .ioctl = pip_ioctl,
};

/* ---------------- select/poll 的就绪判定 ---------------- */
/* 返回 bit0=可读（缓冲里有数据） / bit1=可写（还有空间）；
 * -1 表示这个 fd 不是 pipe（调用方沿用原有判定）。
 * 用户态普遍"先 poll 再 read"，就绪状态必须真实：读空管道返回 0 会被当成
 * EOF（Xorg 喂 keymap 给 xkbcomp 就这样失败过）。 */
int pipe_poll_ready(int fd) {
  thread_t* current = thread_current();
  fd_t* f = thread_find_fd_id(current, fd);
  vnode_t* node;
  pipe_t* p;
  int ready = 0;
  if (f == NULL || f->data == NULL) {
    return -1;
  }
  node = (vnode_t*)f->data;
  if (node->op != &pipe_read_ops && node->op != &pipe_write_ops) {
    return -1;
  }
  p = (pipe_t*)node->device;
  if (p == NULL) {
    return -1;
  }
  pipe_lock(p);
  if (p->count > 0) {
    ready |= 1;
  }
  if (p->count < p->size) {
    ready |= 2;
  }
  pipe_unlock(p);
  return ready;
}

int pipe_create_pair(u32 size, vnode_t** read_node, vnode_t** write_node) {
  pipe_t* p = pipe_create(size);
  vnode_t* r;
  vnode_t* w;
  if (p == NULL || read_node == NULL || write_node == NULL) {
    pipe_destroy(p);
    return -1;
  }
  r = vfs_create_node("pipe-r", 0);
  w = vfs_create_node("pipe-w", 0);
  if (r == NULL || w == NULL) {
    pipe_destroy(p);
    return -1;
  }
  r->device = p;
  r->op = &pipe_read_ops;
  w->device = p;
  w->op = &pipe_write_ops;
  *read_node = r;
  *write_node = w;
  return 0;
}
