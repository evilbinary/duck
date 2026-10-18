/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 10/18/26
 * 邮箱: rootdebug@163.com
 ********************************************************************/
/* 页交换（swap）backing 层实现——阶段 0。对外接口见同目录 swap.h。
 *
 * 【两种 backing，运行期都不经过 fd】
 *   - 分区：device_t 的 ioctl(IOC_WRITE_OFFSET) + read/write，连续扇区；
 *     IOC 常量按平台取（与 modules/fatfs/fatfs.c 同一套条件编译）。
 *   - 文件：vfs_find + vopen 常开 FIL，之后 vread/vwrite 按页偏移 IO。
 *     不用 sys_open 的 fd：fd 挂在线程上，持 fd 的内核线程退出时 fd 会被
 *     回收（连带 vclose → FIL 关闭）；vopen 只操作 vnode，没有这个生命周期
 *     问题。文件不需要预分配：f_write 会自动扩展，换出页本来就是按需落盘。
 *
 * 【并发】槽位 bitmap 与 backing 状态由 swap_lock（rt_mutex，可重入）保护；
 * 页 IO 本身不加锁——调用方（回收路径）持锁选槽，IO 期间槽已被标记占用，
 * 两个线程不会对同一槽并发 IO。
 *
 * 【为什么 v1 不把文件拍成 LBA 表】缺页路径持 vfs_biglock 时重入 vwrite 是
 * 安全的（rt_mutex 按 owner+depth 重入，见 rt_mutex.c）；拍 LBA 绕开 fatfs
 * 属于性能优化，留到阶段 3，回退也方便（本模块单文件）。 */
#include "swap.h"

#ifdef CONFIG_SWAP

#include "kernel/fd.h"
#include "kernel/page.h" /* page_swap_ops_register：向内核注册换出/换入 hook */
#include "kernel/page_ref.h"
#include "kernel/rt_mutex.h"
#include "kernel/stat.h"
#include "kernel/string.h" /* kmemcmp / kstrlen */
#include "modules/sysconf/sysconf.h"
#include "posix/sysfn.h" /* sys_open_kernel / sys_close：文件形式缺文件时创建 */

/* 回收用的 arch/内核接口：page_unmap_on 各 arch mm.c 实现（签名按架构是
 * (u64*,u64) 或 (u32*,u32)，不在此重声明——与 page.c/memory.c 一致走
 * 既有可见性）。物理页归还走 memory_free_phys_page（memory.c），
 * 不直接碰 memory_lock。 */

#if defined(ARM) || defined(ARM64) || defined(__aarch64__)
#include "mmc/sdhci.h"
#else
#include "ahci/ahci.h"
#endif

#define SWAP_MAGIC "YYSW" /* 头部魔数，防止把普通分区/文件当 swap 挂上 */
#define SWAP_VERSION 1

/* 头部页布局（只占页首 16 字节，其余保留） */
typedef struct swap_header {
  char magic[4];
  u32 version;
  u32 nslots; /* 数据槽总数（不含头部槽） */
  u32 pagesz; /* PAGE_SIZE，跨平台镜像互挂时能立刻发现不兼容 */
} swap_header_t;

typedef enum swap_kind { SWAP_BK_NONE = 0, SWAP_BK_PARTITION, SWAP_BK_FILE } swap_kind_t;

typedef struct swap_backing {
  swap_kind_t kind;
  device_t* dev;   /* SWAP_BK_PARTITION */
  vnode_t* node;   /* SWAP_BK_FILE：vopen 常开的 vnode，FIL 存在 node->data */
  u32 nslots;      /* 数据槽数 */
  u32* used;       /* 槽位 bitmap，1 bit/槽 */
  u32 used_words;
} swap_backing_t;

static swap_backing_t swap_bk;
static rt_mutex_t swap_lock;
static int swap_lock_inited;
/* 分区形式的 IO 中转页（内核堆）：换出/换入的缓冲常是用户 VA，块设备驱动
 * （sdhci 的 DMA/PIO 路径）不保证能直接对用户映射做 IO，统一经内核页中转。
 * 文件形式走 fatfs（内部有自己的窗口缓冲），不需要。 */
static void* swap_bounce;

/* ---------------------------------------------------------------- bitmap */

static int swap_bit_test(u32 slot) {
  return (swap_bk.used[slot >> 5] >> (slot & 31)) & 1;
}

static void swap_bit_set(u32 slot) { swap_bk.used[slot >> 5] |= 1u << (slot & 31); }

static void swap_bit_clear(u32 slot) { swap_bk.used[slot >> 5] &= ~(1u << (slot & 31)); }

/* ------------------------------------------------------------------ IO */

/* 头部占用磁盘页 0，数据槽 slot 的落盘页号 = slot + 1 */
static u64 swap_disk_offset(u32 slot) { return ((u64)slot + 1) * PAGE_SIZE; }

static int swap_dev_io(u32 slot, void* page, int is_write) {
  device_t* dev = swap_bk.dev;
  u64 off = swap_disk_offset(slot);
  void* buf = page;
  size_t done;
  if (dev == NULL) {
    return -1;
  }
  if (swap_bounce != NULL) {
    buf = swap_bounce;
    if (is_write) {
      kmemcpy(buf, page, PAGE_SIZE);
    }
  }
  if (dev->ioctl != NULL) {
    dev->ioctl(dev, IOC_WRITE_OFFSET, (u32)off);
  }
  if (is_write) {
    done = dev->write(dev, buf, PAGE_SIZE);
  } else {
    done = dev->read(dev, buf, PAGE_SIZE);
  }
  if (done != PAGE_SIZE) {
    log_error("swap: dev io slot %u %s got %d/%d\n", slot,
              is_write ? "write" : "read", (int)done, (int)PAGE_SIZE);
    return -1;
  }
  if (buf != page) {
    kmemcpy(page, buf, PAGE_SIZE);
  }
  return 0;
}

static int swap_file_io(u32 slot, void* page, int is_write) {
  vnode_t* node = swap_bk.node;
  u64 off = swap_disk_offset(slot);
  u32 done;
  if (node == NULL) {
    return -1;
  }
  if (is_write) {
    done = vwrite(node, (u32)off, PAGE_SIZE, (u8*)page);
  } else {
    done = vread(node, (u32)off, PAGE_SIZE, (u8*)page);
  }
  if (done != PAGE_SIZE) {
    log_error("swap: file io slot %u %s got %d/%d\n", slot,
              is_write ? "write" : "read", (int)done, (int)PAGE_SIZE);
    return -1;
  }
  return 0;
}

/* ------------------------------------------------------------ 挂载/格式化 */

static void swap_format_header(void* page) {
  swap_header_t* h = (swap_header_t*)page;
  kmemset(page, 0, PAGE_SIZE);
  kmemcpy(h->magic, SWAP_MAGIC, 4);
  h->version = SWAP_VERSION;
  h->nslots = swap_bk.nslots;
  h->pagesz = PAGE_SIZE;
}

static int swap_check_header(void* page) {
  swap_header_t* h = (swap_header_t*)page;
  if (kmemcmp(h->magic, SWAP_MAGIC, 4) != 0) {
    return -1;
  }
  if (h->version != SWAP_VERSION || h->nslots != swap_bk.nslots ||
      h->pagesz != PAGE_SIZE) {
    return -1;
  }
  return 0;
}

/* 槽 0 = 头部：先读旧头校验设备可用，校验不过就重写（格式化）。
 * 无论哪种 backing，挂载成功后全部数据槽都视为空闲（无脏恢复，见 swap.h）。 */
static int swap_backing_prepare(void) {
  void* page = kmalloc(PAGE_SIZE, KERNEL_TYPE);
  int rc;
  if (page == NULL) {
    log_error("swap: no mem for header page\n");
    return -1;
  }
  rc = swap_bk.kind == SWAP_BK_PARTITION ? swap_dev_io(0, page, 0)
                                         : swap_file_io(0, page, 0);
  if (rc == 0 && swap_check_header(page) == 0) {
    log_info("swap: header ok (%u slots)\n", swap_bk.nslots);
  } else {
    log_info("swap: format header (%u slots)\n", swap_bk.nslots);
    swap_format_header(page);
    rc = swap_bk.kind == SWAP_BK_PARTITION ? swap_dev_io(0, page, 1)
                                           : swap_file_io(0, page, 1);
    if (rc != 0) {
      kfree(page);
      return -1;
    }
  }
  kfree(page);
  return 0;
}

static void swap_backing_reset(void) {
  if (swap_bk.used != NULL) {
    kfree(swap_bk.used);
  }
  if (swap_bounce != NULL) {
    kfree(swap_bounce);
    swap_bounce = NULL;
  }
  kmemset(&swap_bk, 0, sizeof(swap_bk));
}

static int swap_backing_install(swap_kind_t kind, device_t* dev, vnode_t* node,
                                u32 nslots) {
  u32 words;
  if (nslots == 0 || nslots > 0x100000u) { /* 上限 4GB@4KB，防配置写错 */
    log_error("swap: bad slot count %u\n", nslots);
    return -1;
  }
  swap_backing_reset();
  words = (nslots + 31) / 32;
  swap_bk.used = kmalloc(words * 4, KERNEL_TYPE);
  if (swap_bk.used == NULL) {
    log_error("swap: no mem for bitmap (%u slots)\n", nslots);
    return -1;
  }
  kmemset(swap_bk.used, 0, words * 4);
  swap_bk.kind = kind;
  swap_bk.dev = dev;
  swap_bk.node = node;
  swap_bk.nslots = nslots;
  swap_bk.used_words = words;
  swap_bit_set(0); /* 头部槽常占（逻辑上预留，落盘在页 0） */
  if (kind == SWAP_BK_PARTITION) {
    swap_bounce = kmalloc(PAGE_SIZE, KERNEL_TYPE);
    if (swap_bounce == NULL) {
      log_error("swap: no mem for io bounce page\n");
      swap_backing_reset();
      return -1;
    }
  }
  if (swap_backing_prepare() != 0) {
    swap_backing_reset();
    return -1;
  }
  log_info("swap: attached %s, %u slots (%u MB)\n",
           kind == SWAP_BK_PARTITION ? "partition" : "file", nslots,
           nslots * PAGE_SIZE / (1024 * 1024));
  return 0;
}

/* ------------------------------------------------------------ 两种形式 */

static int swap_attach_partition(const char* name) {
  device_t* dev;
  dstat_t st;
  u32 nslots;
  char nbuf[16];
  if (name == NULL || kstrlen(name) == 0 || kstrlen(name) >= sizeof(nbuf)) {
    return -1;
  }
  dev = device_find_name((char*)name);
  if (dev == NULL) {
    log_warn("swap: device %s not found\n", name);
    return -1;
  }
  if (dev->stat == NULL) {
    log_error("swap: device %s has no stat\n", name);
    return -1;
  }
  kmemset(&st, 0, sizeof(st));
  dev->stat(dev, &st);
  if (st.size < 2 * PAGE_SIZE) {
    log_error("swap: device %s too small (%d bytes)\n", name, (int)st.size);
    return -1;
  }
  nslots = (u32)(st.size / PAGE_SIZE) - 1; /* 减头部槽 */
  return swap_backing_install(SWAP_BK_PARTITION, dev, NULL, nslots);
}

static int swap_attach_file(const char* path) {
  vnode_t* node;
  struct stat st;
  u32 nslots;
  int size_mb;
  u64 fsize;
  int ret;
  if (path == NULL || path[0] != '/') {
    log_error("swap: file path must be absolute: %s\n", path != NULL ? path : "");
    return -1;
  }
  node = vfs_find(NULL, (u8*)path);
  if (node == NULL) {
    /* 不存在就创建（fatfs 的 O_CREAT 走 fd 路径；创建完即可关 fd，
     * 之后用 vopen 在 vnode 上常开 FIL，见文件头注释）。 */
    u32 fd = sys_open_kernel(path, O_CREAT | O_RDWR);
    if ((int)fd < 0) {
      log_error("swap: cannot create %s\n", path);
      return -1;
    }
    sys_close(fd);
    node = vfs_find(NULL, (u8*)path);
    if (node == NULL) {
      log_error("swap: created %s but not found\n", path);
      return -1;
    }
  }
  ret = (int)vopen(node, O_RDWR);
  if (ret == (u32)-1 || ret == 0) {
    log_error("swap: cannot open %s\n", path);
    return -1;
  }
  /* 目标大小：文件已有内容 → 用文件大小；否则用 size_mb 配置（默认值见 config.h） */
  kmemset(&st, 0, sizeof(st));
  if (vioctl(node, IOC_STAT, &st) == 0 && st.st_size > 0) {
    fsize = (u64)st.st_size;
  } else {
    size_mb = sysconf_get_int("swap", "size_mb", CONFIG_SWAP_FILE_MB);
    if (size_mb < 1) {
      size_mb = 1;
    }
    if (size_mb > 4096) {
      size_mb = 4096;
    }
    fsize = (u64)size_mb * 1024 * 1024;
  }
  if (fsize < 2 * PAGE_SIZE) {
    log_error("swap: %s too small\n", path);
    return -1;
  }
  nslots = (u32)(fsize / PAGE_SIZE) - 1; /* 减头部槽 */
  return swap_backing_install(SWAP_BK_FILE, NULL, node, nslots);
}

/* ------------------------------------------------ 换出/换入记录（阶段 1/2）
 *
 * 【为什么不写进 PTE】各 arch PTE 编码不同（armv5 连 present=0 时地址域的
 * 布局都不一样），为每个 arch 改 PTE 风险大；用旁路表，键 (vm, va)。
 * 【锁】swap_lock 只护记录表与槽位 bitmap，【绝不持锁做 IO】——文件形式的
 * 换出/换入要进 vfs_biglock，而调用方（缺页路径）本身就持着 vfs_biglock：
 * 若持 swap_lock 做 IO，"A 持 swap_lock 等 vfs / B 持 vfs 等 swap_lock" 的
 * 锁序交叉就来了（rt_mutex 可重入救不了跨线程的场景）。 */

#define SWAP_REC_BUCKETS 256 /* 2 的幂；哈希键 = vm ^ va */

typedef struct swap_rec {
  void* vm;
  vaddr_t va;
  u32 slot;
  struct swap_rec* next;
} swap_rec_t;

static swap_rec_t* swap_recs[SWAP_REC_BUCKETS];

static u32 swap_rec_bucket(void* vm, vaddr_t va) {
  u32 h = (u32)(unsigned long)vm ^ (u32)((unsigned long)va >> 12);
  h ^= h >> 16;
  return h & (SWAP_REC_BUCKETS - 1);
}

/* 以下三个都要求已持 swap_lock */
static swap_rec_t* swap_rec_find(void* vm, vaddr_t va) {
  swap_rec_t* r;
  for (r = swap_recs[swap_rec_bucket(vm, va)]; r != NULL; r = r->next) {
    if (r->vm == vm && r->va == va) {
      return r;
    }
  }
  return NULL;
}

static int swap_rec_add(void* vm, vaddr_t va, u32 slot) {
  u32 b = swap_rec_bucket(vm, va);
  swap_rec_t* r;
  for (r = swap_recs[b]; r != NULL; r = r->next) {
    if (r->vm == vm && r->va == va) {
      return -1; /* 该页已在换出中：并发回收同页时第二个直接放弃 */
    }
  }
  r = kmalloc(sizeof(swap_rec_t), KERNEL_TYPE);
  if (r == NULL) {
    return -1;
  }
  r->vm = vm;
  r->va = va;
  r->slot = slot;
  r->next = swap_recs[b];
  swap_recs[b] = r;
  return 0;
}

static void swap_rec_del(void* vm, vaddr_t va) {
  swap_rec_t** pp = &swap_recs[swap_rec_bucket(vm, va)];
  while (*pp != NULL) {
    swap_rec_t* r = *pp;
    if (r->vm == vm && r->va == va) {
      *pp = r->next;
      kfree(r);
      return;
    }
    pp = &r->next;
  }
}

/* 以下四个经 page_swap_ops_t 注册给内核（见 swap_init），不对外导出 */
static u32 swap_lookup(void* vm, vaddr_t va) {
  swap_rec_t* r;
  u32 slot = 0;
  if (vm == NULL || !swap_attached()) {
    return 0;
  }
  va &= ~(vaddr_t)PAGE_SIZE - (vaddr_t)1;
  rt_mutex_lock(&swap_lock);
  r = swap_rec_find(vm, va);
  if (r != NULL) {
    slot = r->slot;
  }
  rt_mutex_unlock(&swap_lock);
  return slot;
}

static void swap_forget(void* vm, vaddr_t va) {
  if (vm == NULL || !swap_attached()) {
    return;
  }
  va &= ~(vaddr_t)PAGE_SIZE - (vaddr_t)1;
  rt_mutex_lock(&swap_lock);
  swap_rec_del(vm, va);
  rt_mutex_unlock(&swap_lock);
}

static void swap_drop_vm(void* vm) {
  u32 b;
  if (vm == NULL) {
    return;
  }
  rt_mutex_lock(&swap_lock);
  for (b = 0; b < SWAP_REC_BUCKETS; b++) {
    swap_rec_t** pp = &swap_recs[b];
    while (*pp != NULL) {
      swap_rec_t* r = *pp;
      if (r->vm == vm) {
        *pp = r->next;
        if (swap_attached()) {
          swap_bit_clear(r->slot);
        }
        kfree(r);
      } else {
        pp = &r->next;
      }
    }
  }
  rt_mutex_unlock(&swap_lock);
}

/* ------------------------------------------------------------ 同步回收 */

/* 换出一页：选槽 → 记录 → 落盘 → 摘映射 → 还页池。
 * 【顺序】记录必须先于落盘/摘映射：缺页路径看到"phy==NULL 且有记录"才走
 * 换入，两个条件之间不能出现"没映射也没记录"的空窗（那会当匿名页补 0，
 * 数据就丢了）。IO 失败则回滚记录与槽位，跳过该页。 */
static int swap_evict_one(vmemory_t* vm, vaddr_t va, u32 skip_pa) {
  void* phy;
  u32 pa;
  u32 slot = 0;
  int have_slot = 0;

  if (va >= (vaddr_t)USER_VA_END) {
    return -1; /* 内核线程的 VMA 搬在 USER_VA_END 之上，绝不动 */
  }
  phy = page_v2p(vm->upage, (void*)va);
  if (phy == NULL) {
    return -1;
  }
  pa = (u32)(unsigned long)phy;
  if (pa == skip_pa) {
    return -1; /* 调用方正在处理的页（如 COW 源页） */
  }
  if (page_ref_get(pa) != 1) {
    return -1; /* COW/共享页：别人还在用，换出会踩对方 */
  }
  rt_mutex_lock(&swap_lock);
  if (swap_alloc_slot(&slot) == 0) {
    if (swap_rec_add(vm, va, slot) == 0) {
      have_slot = 1;
    } else {
      swap_free_slot(slot);
    }
  }
  rt_mutex_unlock(&swap_lock);
  if (!have_slot) {
    return -1; /* swap 满或记录表耗尽 */
  }

  /* 落盘（不持锁，见记录节注释）；换出前 clean，保证 DRAM 里是最新内容 */
  cpu_flush_dcache_range((unsigned long)va, (unsigned long)va + PAGE_SIZE);
  if (swap_write_page(slot, (void*)va) != 0) {
    rt_mutex_lock(&swap_lock);
    swap_rec_del(vm, va);
    swap_bit_clear(slot);
    rt_mutex_unlock(&swap_lock);
    return -1;
  }

  /* 摘映射 + 还页池（走 memory.c 的统一入口：引用计数闸 + 统计） */
  page_unmap_on(vm->upage, va);
  memory_free_phys_page(phy);
  return 0;
}

/* fork 前换回：一条条处理（摘下记录 → valloc → 读回 → 归还槽位），
 * valloc/read 失败就把记录原样挂回，留待缺页路径兜底。
 * 【不能拿着记录指针直接遍历】valloc 内部可能触发 reclaim —— 它会对同一张
 * 记录表增删（换出的都是常驻页，不会动我们这条 phy==NULL 的记录，但同桶
 * 链表会变）——所以每次摘一条、处理完再找下一条，指针不跨调用存活。 */
static int swap_swapback_one(void* vm) {
  u32 b;
  rt_mutex_lock(&swap_lock);
  for (b = 0; b < SWAP_REC_BUCKETS; b++) {
    swap_rec_t** pp = &swap_recs[b];
    while (*pp != NULL) {
      if ((*pp)->vm == vm) {
        swap_rec_t* r = *pp;
        vaddr_t va = r->va;
        u32 slot = r->slot;
        void* vret;
        int ok;
        *pp = r->next;
        kfree(r);
        rt_mutex_unlock(&swap_lock);

        vret = valloc((void*)va, PAGE_SIZE);
        ok = (vret != NULL) && (swap_read_page(slot, (void*)va) == 0);

        rt_mutex_lock(&swap_lock);
        if (ok) {
          swap_bit_clear(slot);
        } else {
          swap_rec_add(vm, va, slot); /* 挂回，缺页路径下次再试 */
        }
        rt_mutex_unlock(&swap_lock);
        return ok ? 0 : -1;
      }
      pp = &(*pp)->next;
    }
  }
  rt_mutex_unlock(&swap_lock);
  return -1; /* 没有记录了 */
}

static int swap_swapback_vm(void* vm) {
  int n = 0;
  if (vm == NULL || !swap_attached()) {
    return 0;
  }
  while (swap_swapback_one(vm) == 0) {
    n++;
  }
  if (n > 0) {
    log_info("swap: fork swapback %d page(s)\n", n);
  }
  return n;
}

/* 只动"有换入路径"的区：
 *   · mmap 子区（heap->child）：大分配/文件映射都在这，优先扫；
 *   · 堆主区（brk 长出来的匿名页）。
 * 跳过：DEV/FB（设备寄存器/显存绝不能换）、栈（页少且热，缺页代价高）、
 * EXEC 主区（代码页换出后每次调用都缺页，性能塌方；阶段 3 再做
 * "只读文件页直接丢弃重读"）。
 * 注意 reclaim 永远只扫【当前线程】的地址空间：调用点都在缺页/分配路径上，
 * current 就是要腾内存的那个进程。 */
static int swap_reclaim(u32 npages, u32 skip_pa) {
  thread_t* cur = thread_current();
  vmemory_t* vm;
  vmemory_area_t* a;
  int freed = 0;

  if (npages == 0 || !swap_attached() || cur == NULL || cur->vm == NULL) {
    return 0;
  }
  vm = cur->vm;
  if (vm->upage == NULL || (vm->kpage != NULL && vm->upage == vm->kpage)) {
    return 0; /* 内核线程（共享内核页目录）没有可换的用户页 */
  }

  for (a = vm->vma; a != NULL && freed < (int)npages; a = a->next) {
    vmemory_area_t* c;
    vaddr_t va;
    if (a->flags == MEMORY_DEV || a->flags == MEMORY_FB ||
        a->flags == MEMORY_FREE || a->vaddr >= (vaddr_t)USER_VA_END) {
      continue;
    }
    /* 子区（mmap）优先：大块分配、文件映射都在这，回收收益最大 */
    for (c = a->child; c != NULL && freed < (int)npages; c = c->next) {
      if (c->flags == MEMORY_DEV || c->flags == MEMORY_FB ||
          c->flags == MEMORY_FREE) {
        continue;
      }
      for (va = c->vaddr; va < c->vend && freed < (int)npages; va += PAGE_SIZE) {
        if (swap_evict_one(vm, va, skip_pa) == 0) {
          freed++;
        }
      }
    }
    if (a->flags == MEMORY_STACK || a->flags == MEMORY_EXEC) {
      continue;
    }
    for (va = a->vaddr; va < a->vend && freed < (int)npages; va += PAGE_SIZE) {
      if (swap_evict_one(vm, va, skip_pa) == 0) {
        freed++;
      }
    }
  }
  if (freed > 0) {
    log_info("swap: reclaimed %d page(s)\n", freed);
  }
  return freed;
}

/* ---------------------------------------------------------- 对外接口 */

void swap_conf_attach(void) {
  char val[64];
  int rc = -1;
  if (!swap_lock_inited) {
    rt_mutex_init(&swap_lock);
    swap_lock_inited = 1;
  }
  rt_mutex_lock(&swap_lock);
  if (swap_bk.kind != SWAP_BK_NONE) {
    rt_mutex_unlock(&swap_lock);
    return; /* 已挂载，重复触发（execve 多次读 conf）直接忽略 */
  }
  if (sysconf_get_str("swap", "file", val, sizeof(val)) == 0) {
    rc = swap_attach_file(val);
  } else if (sysconf_get_str("swap", "device", val, sizeof(val)) == 0) {
    rc = swap_attach_partition(val);
  }
  rt_mutex_unlock(&swap_lock);
  if (rc != 0) {
    log_info("swap: no backing configured, disabled\n");
  }
}

int swap_attached(void) {
#ifdef CONFIG_SWAP
  return swap_bk.kind != SWAP_BK_NONE;
#else
  return 0;
#endif
}

u32 swap_slot_count(void) { return swap_attached() ? swap_bk.nslots : 0; }

u32 swap_free_slots(void) {
  u32 i, n = 0;
  if (!swap_attached()) {
    return 0;
  }
  rt_mutex_lock(&swap_lock);
  for (i = 1; i < swap_bk.nslots; i++) { /* 0 = 头部，不算 */
    if (!swap_bit_test(i)) {
      n++;
    }
  }
  rt_mutex_unlock(&swap_lock);
  return n;
}

int swap_alloc_slot(u32* slot) {
  u32 i;
  if (!swap_attached() || slot == NULL) {
    return -1;
  }
  rt_mutex_lock(&swap_lock);
  for (i = 1; i < swap_bk.nslots; i++) {
    if (!swap_bit_test(i)) {
      swap_bit_set(i);
      *slot = i;
      rt_mutex_unlock(&swap_lock);
      return 0;
    }
  }
  rt_mutex_unlock(&swap_lock);
  return -1; /* swap 满：上层决定是否报错（物理页也耗尽才走到这） */
}

void swap_free_slot(u32 slot) {
  if (!swap_attached() || slot == 0 || slot >= swap_bk.nslots) {
    return;
  }
  rt_mutex_lock(&swap_lock);
  swap_bit_clear(slot);
  rt_mutex_unlock(&swap_lock);
}

int swap_read_page(u32 slot, void* page) {
  if (!swap_attached() || slot == 0 || slot >= swap_bk.nslots || page == NULL) {
    return -1;
  }
  if (swap_bk.kind == SWAP_BK_PARTITION) {
    return swap_dev_io(slot, page, 0);
  }
  return swap_file_io(slot, page, 0);
}

int swap_write_page(u32 slot, void* page) {
  if (!swap_attached() || slot == 0 || slot >= swap_bk.nslots || page == NULL) {
    return -1;
  }
  if (swap_bk.kind == SWAP_BK_PARTITION) {
    return swap_dev_io(slot, page, 1);
  }
  return swap_file_io(slot, page, 1);
}

#else /* !CONFIG_SWAP：编译期整体裁掉，接口留空实现保证可链接 */

int swap_attached(void) { return 0; }
u32 swap_slot_count(void) { return 0; }
u32 swap_free_slots(void) { return 0; }
int swap_alloc_slot(u32* slot) { (void)slot; return -1; }
void swap_free_slot(u32 slot) { (void)slot; }
int swap_read_page(u32 slot, void* page) { (void)slot; (void)page; return -1; }
int swap_write_page(u32 slot, void* page) { (void)slot; (void)page; return -1; }
void swap_conf_attach(void) {}

#endif /* CONFIG_SWAP */

/* ---------------- 模块入口 ----------------
 * 注册在 fatfs 之后、sysconf 之前（app/init/module.c）：init 做两件事——
 *   1) 向内核注册换出/换入 hook（page_swap_ops，见 kernel/memory.h）；
 *   2) 登记 sysconf 回调，配置就绪时挂载 backing（swap_conf_attach）。
 * sysconf_apply 已跑过的情况下（异常时序）就地挂载一次兜底。 */
static const page_swap_ops_t swap_page_ops = {
    .lookup = swap_lookup,
    .forget = swap_forget,
    .drop_vm = swap_drop_vm,
    .reclaim = swap_reclaim,
    .read_page = swap_read_page,
    .swapback_vm = swap_swapback_vm,
};

int swap_init(void) {
#ifdef CONFIG_SWAP
  rt_mutex_init(&swap_lock);
  swap_lock_inited = 1;
  page_swap_ops_register(&swap_page_ops);
  if (sysconf_loaded()) {
    swap_conf_attach();
  } else {
    sysconf_on_ready(swap_conf_attach);
  }
  log_info("swap module init\n");
#endif
  return 0;
}

void swap_exit(void) { log_info("swap exit\n"); }

module_t swap_module = {.name = "swap", .init = swap_init, .exit = swap_exit};
