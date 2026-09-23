/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "pipe.h"

#include "kernel/kernel.h"

size_t pipe_buffer_read_wait(buffer_t* buffer) {
  rw_queue_t* rwq = buffer->data;
  rw_queue_read_wait(rwq);
  return 1;
}

size_t pipe_buffer_read_notify(buffer_t* buffer) {
  rw_queue_t* rwq = buffer->data;
  rw_queue_read_notify(rwq);
  return 1;
}

size_t pipe_buffer_write_wait(buffer_t* buffer) {
  rw_queue_t* rwq = buffer->data;
  rw_queue_write_wait(rwq);
  return 1;
}

size_t pipe_buffer_write_notify(buffer_t* buffer) {
  rw_queue_t* rwq = buffer->data;
  rw_queue_write_notify(rwq);
  return 1;
}

pipe_t* pipe_create(u32 size) {
  pipe_t* pipe = kmalloc(sizeof(pipe_t),KERNEL_TYPE);
  pipe->wait_queue = rw_queue_create(PIPE_WAIT_QUEUE_SIZE);
  /* 【不做内核阻塞】buffer_read/write 在空/满时会调用 wait 回调，而 wait 回调
   * 走 thread_wait()→schedule_switch() 在 vread() 持有 vfs_biglock 期间阻塞：
   * 别的线程一碰 vfs 就在 rt_mutex_lock(vfs_biglock) 里自旋死锁（system() 实测：
   * 父进程读 pipe 阻塞并持有 vfs_biglock，子进程 exec /bin/sh 时取锁自旋卡死）。
   * 本内核的设计本就是"内核不阻塞、等待交给调用者在用户态轮询/靠 waitpid"，
   * 所以这里不挂阻塞回调：pipe 空读返回 0，写满就停。system()/posix_spawn 的
   * 同步改由随后的 waitpid（schedule_switch）承担。 */
  pipe->buffer =
      buffer_create(size, NULL, NULL,
                    pipe_buffer_write_notify, pipe_buffer_read_notify);
  pipe->buffer->data = pipe->wait_queue;
  return pipe;
}

u32 pip_read(vnode_t* node, u32 offset, u32 size, u8* buffer) {
  pipe_t* pipe = node->device;
  if (pipe == NULL) {
    kprintf("pipe read null\n");
    return -1;
  }
  u32 ret = 0;
  ret = buffer_read(pipe->buffer, buffer, size);
  kprintf("pipe read: %s size:%d ret %d\n", buffer,size, ret);
  return ret;
}

u32 pip_write(vnode_t* node, u32 offset, u32 size, u8* buffer) {
  pipe_t* pipe = node->device;
  if (pipe == NULL) {
    kprintf("pipe write null\n");
    return -1;
  }
  kprintf("pipe write: %s size:%d\n",buffer, size);
  u32 ret = buffer_write(pipe->buffer, buffer, size);
  return ret;
}

vnode_t* pipe_make(u32 size) {
  vnode_t* node = vfs_create_node("pipe", 0);
  node->device = pipe_create(size);
  voperator_t* op=kmalloc(sizeof(voperator_t),KERNEL_TYPE);
  node->op=op;
  op->read = pip_read;
  op->write = pip_write;
  return node;
}
