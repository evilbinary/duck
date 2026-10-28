/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * memfd_create(279)：匿名内存文件（wayland wl_shm 的标准来路）。
 *
 * 【为什么不能是文件后端】最初实现是 /tmp 下的真实文件 + ftruncate +
 * 文件 MAP_SHARED。实测跨进程不成立：本内核的文件共享映射没有页缓存，
 * 写入停在映射方页里、munmap/exit 才 flush 回 FAT；另一进程此时缺页
 * 从文件读，只能拿到磁盘旧内容（wl_shm 客户端画的像素，合成器读不到）。
 * Linux 语义是"同一 inode 的页"，memfd 更是纯内存页。
 *
 * 因此这里实现**共享页对象**：
 *   - memfd_create 建 vnode + 对象（页数组，初始 0 页）
 *   - ftruncate 增减页：mm_alloc_page + cpu_zero_phy_page，对象持 1 份引用
 *   - mmap(MAP_SHARED) 缺页时（duck/kernel/page.c 的 memfd_fault_hook）
 *     把同一物理页 page_ref_inc 后装配进调用者页表 ⇒ 多进程看到同一份页
 *   - fd 关闭释放对象：逐页放引用，归零自然回收
 * 钩子在 sys_fn_init 注册（见 sysfn.h 的 memfd_* 声明）。
 ********************************************************************/
#include "sysfn.h"

#include "arch/pmemory.h" /* mm_alloc_page / mm_free_page */
#include "kernel/error.h"
#include "kernel/fd.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "kernel/page_ref.h"
#include "kernel/thread.h"

#define log_debug

/* linux/memfd.h 的 uapi 位 */
#define MFD_CLOEXEC_ 0x0001u
#define MFD_ALLOW_SEALING_ 0x0002u

#define MEMFD_MAGIC 0x3E3FD000u
#define MEMFD_NAME_MAX 56
#define MEMFD_MAX_PAGES 8192 /* 单对象上限 32MB，wl_shm 双缓冲足够 */

struct memfd_obj {
  u32 magic;
  u32 npages;
  void** pages; /* 物理页地址（对象持每页 1 份引用） */
};

static void memfd_free_pages(void** pages, u32 npages) {
  u32 i;
  if (pages == NULL) {
    return;
  }
  for (i = 0; i < npages; i++) {
    if (pages[i] != NULL) {
      /* 放掉对象持有的那一份；还有映射在用时留在页池外（ref>0） */
      if (page_ref_dec((u32)(uintptr_t)pages[i]) == 0) {
        mm_free_page(pages[i]);
      }
    }
  }
  kfree(pages);
}

static u32 memfd_node_close(vnode_t* node) {
  struct memfd_obj* obj;
  if (node == NULL) {
    return 0;
  }
  obj = (struct memfd_obj*)node->device;
  if (obj != NULL) {
    if (obj->magic == MEMFD_MAGIC) {
      memfd_free_pages(obj->pages, obj->npages);
    }
    kfree(obj);
    node->device = NULL;
  }
  if (node->op != NULL) {
    kfree(node->op);
    node->op = NULL;
  }
  if (node->name != NULL) {
    kfree(node->name);
    node->name = NULL;
  }
  kfree(node);
  return 0;
}

/* ---- vnode 操作表：共享页接口（内核 page.c / vma.c / sysfn_fs.c 按
 * op->fault / op->truncate 分派，不需要认识 memfd） ---- */

static void* memfd_op_fault(vnode_t* node, u64 off) {
  struct memfd_obj* obj;
  u64 idx;
  if (node == NULL || node->device == NULL) {
    return NULL;
  }
  obj = (struct memfd_obj*)node->device;
  if (obj->magic != MEMFD_MAGIC) {
    return NULL;
  }
  idx = off >> 12;
  if (idx >= obj->npages) {
    return NULL; /* 超长：由缺页路径补 0 */
  }
  return obj->pages[idx];
}

static int memfd_op_truncate(vnode_t* node, u64 size) {
  struct memfd_obj* obj;
  u32 want;
  u32 i;
  if (node == NULL || node->device == NULL) {
    return -22;
  }
  obj = (struct memfd_obj*)node->device;
  if (obj->magic != MEMFD_MAGIC) {
    return -22;
  }
  want = (u32)((size + PAGE_SIZE - 1) / PAGE_SIZE);
  if (want > MEMFD_MAX_PAGES) {
    want = MEMFD_MAX_PAGES;
  }
  if (want > obj->npages) {
    /* 扩容：换新数组（页地址表），新页分配并清零 */
    void** np = kmalloc(sizeof(void*) * want, KERNEL_TYPE);
    if (np == NULL) {
      return -12;
    }
    kmemset(np, 0, sizeof(void*) * want);
    for (i = 0; i < obj->npages; i++) {
      np[i] = obj->pages[i];
    }
    for (i = obj->npages; i < want; i++) {
      void* pa = mm_alloc_page();
      if (pa == NULL) {
        /* 页池不足：保留已扩部分，长度按实际给（ftruncate 语义允许失败，
         * 这里退化为"尽力"——npages 已扩到 i） */
        want = i;
        break;
      }
      cpu_zero_phy_page(pa, PAGE_SIZE);
      page_ref_set((u32)(uintptr_t)pa, 1); /* 对象持有的那一份 */
      np[i] = pa;
    }
    /* 缩容部分：新数组尾部为空 ⇒ 旧尾部页需要还 */
    for (i = want; i < obj->npages; i++) {
      if (obj->pages[i] != NULL) {
        if (page_ref_dec((u32)(uintptr_t)obj->pages[i]) == 0) {
          mm_free_page(obj->pages[i]);
        }
      }
    }
    if (obj->pages != NULL) {
      kfree(obj->pages);
    }
    obj->pages = np;
    obj->npages = want;
  } else if (want < obj->npages) {
    /* 缩容：尾部页放引用 */
    for (i = want; i < obj->npages; i++) {
      if (obj->pages[i] != NULL) {
        if (page_ref_dec((u32)(uintptr_t)obj->pages[i]) == 0) {
          mm_free_page(obj->pages[i]);
        }
        obj->pages[i] = NULL;
      }
    }
    obj->npages = want;
  }
  log_debug("memfd truncate size=%d npages=%d\n", (int)size, (int)obj->npages);
  return 0;
}

/* ---- syscall ---- */

int sys_memfd_create(unsigned long uname, unsigned long flags) {
  thread_t* current = thread_current();
  char name[MEMFD_NAME_MAX];
  vnode_t* node;
  struct memfd_obj* obj;
  voperator_t* op;
  fd_t* fd;
  int n;
  int i;
  if (current == NULL || uname == 0) {
    return -22; /* -EINVAL */
  }
  if ((flags & ~(MFD_CLOEXEC_ | MFD_ALLOW_SEALING_)) != 0) {
    return -22; /* 未知 flag：Linux 语义 EINVAL */
  }
  kmemset(name, 0, sizeof(name));
  if (sys_copy_from_user(name, (const void*)(uintptr_t)uname,
                         sizeof(name) - 1) < 0) {
    return -14; /* -EFAULT */
  }
  name[sizeof(name) - 1] = 0;
  for (i = 0; i < MEMFD_NAME_MAX - 1 && name[i] != 0; i++) {
    unsigned char ch = (unsigned char)name[i];
    if (ch < 0x20 || ch > 0x7e) {
      name[i] = '?';
    }
  }

  node = vfs_create_node(name[0] != 0 ? name : "memfd", 0);
  obj = kmalloc(sizeof(struct memfd_obj), KERNEL_TYPE);
  op = kmalloc(sizeof(voperator_t), KERNEL_TYPE);
  if (node == NULL || obj == NULL || op == NULL) {
    kfree(obj);
    kfree(op);
    if (node != NULL) {
      kfree(node);
    }
    return -12; /* -ENOMEM */
  }
  kmemset(obj, 0, sizeof(*obj));
  obj->magic = MEMFD_MAGIC;
  node->device = obj;
  node->op = op;
  op->close = memfd_node_close;
  /* 共享页接口：内核 mmap/缺页/ftruncate 按这两个方法分派 */
  op->fault = memfd_op_fault;
  op->truncate = memfd_op_truncate;

  fd = fd_open((u32*)node, DEVICE_TYPE_VIRTUAL, node->name);
  if (fd == NULL) {
    memfd_node_close(node);
    return -12;
  }
  n = thread_add_fd(current, fd);
  if (n < 0) {
    node->device = NULL; /* 防止 close 路径二次释放对象 */
    fd_close(fd);
    memfd_node_close(node);
    return -12;
  }
  if (flags & MFD_CLOEXEC_) {
    fd_t* f = thread_find_fd_id(current, (u32)n);
    if (f != NULL) {
      f->flags |= FD_CLOEXEC;
    }
  }
  log_debug("memfd create fd=%d name=%s\n", n, node->name);
  return n;
}
