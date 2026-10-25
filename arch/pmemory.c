#include "pmemory.h"

/* 【恒等映射上限：建表与页分配器必须一致】
 * boot 建表（mm_parse_map → map_mem_block）只把 RAM 的前 MM_IDENTITY_MAX 映射成
 * 恒等映射；而页分配器（mm_add_block 建的空闲链）若按【整段 RAM】发页，就会发出
 * 【没有身份映射】的物理页 —— 内核凡是按物理地址直访的地方（COW 破写 / fork 深拷贝
 * 的 kmemcpy((void*)pa,…)）都会在【内核里再触发一次翻译故障】⇒ 不收敛 ⇒
 * 缺页风暴 / memory fault at 0（实测 raspi3：ymain 一起来就崩）。
 * 历史上这个 80MB 上限是为了避免"每次 fork 的 page_clone 建上百个 L2 表" ⇒ 卡住
 * （见 mm_parse_map 的注释）；这里只是让【分配器】也守同一个约束。 */
#ifndef MM_IDENTITY_MAX
/* 【恒等映射上限：建表与页分配器必须一致】
 * 默认 0 = 不设上限：恒等映射与页分配器都覆盖实际 RAM 全量。
 * 历史上写死 80MB（PAGE_SIZE*20000）是为了避免"全量恒等映射 ⇒ 每次 fork
 * page_clone 建上百个 L2 表"卡死；现在 page_copy 对 < EXEC_ADDR 的恒等区
 * L2 直接共享（见各 arch 的 mm.c），该开销已消失。写死 80MB 的代价是
 * 128MB 的机器只能用 80MB（实测 miyoo `mem` total 80031k，少用 ~44MB）。
 * 平台若确实需要更小的窗口，可 -DMM_IDENTITY_MAX=<bytes> 覆盖。 */
#define MM_IDENTITY_MAX 0
#endif

#include "kernel/common.h"
#include "kernel/logger.h"
#include "kernel/page.h"
#include "kernel/string.h"

static u32 count = 0;
const size_t align_to = 16;

/* 【前向声明】mm_kernel_heap_watermark 用；定义在同文件 #ifdef 分支后段 */
ullong mm_get_total(void);
ullong mm_get_free(void);

/* 【块尾哨兵占位必须按 8 字节算】堆块布局 [block_t 头][数据(size)][END]：
 * END 只写 4 字节 int，但占位留 8 字节 —— 否则下一个块头落在 4 mod 8 的
 * 地址上，AArch64 上 block_t 的 8 字节字段就是非对齐访问（MMU 未开 ⇒ 对齐
 * 异常；实测 page_create 第一次堆分配就死循环刷 kernel_page_dir is null）。 */
#define YA_END_SIZE 8
extern boot_info_t* boot_info;

#define ALIGN(x, a) (x + (a - 1)) & ~(a - 1)
#define PMEM_ALIGN 16

memory_manager_t mmt;
static void* mm_page_free_list = NULL;

/* COW：释放用户页时的回调，由 kernel 注册（page_ref 的 page_put）；
 * NULL = 直接释放。定义在 arch 通用层，各架构共享。 */
int (*page_put_page)(void* pa) = 0;

// #define DEBUG 1
#define MM_YA_ALLOC 1

#ifdef MM_YA_ALLOC

void mm_add_block(uintptr_t addr, uintptr_t len);

/* YiYiYa：物理 RAM 基址 —— 遍历所有内存块取最小的 origin_addr。
 * 与 mm_get_total() 同源（都来自 boot_info 的 memory 表），不写死任何平台常量 ✓。 */
ullong mm_get_base(void) {
  ullong base = 0;
  mem_block_t* p = mmt.blocks;
  for (; p != NULL; p = p->next) {
    if (base == 0 || (ullong)p->origin_addr < base) {
      base = (ullong)p->origin_addr;
    }
  }
  return base;
}

void ya_alloc_init() {
#ifdef LX6
  kprintf("Y0\n");
#endif
  for (int i = 0; i < boot_info->memory_number; i++) {
    memory_info_t* mem = (memory_info_t*)&boot_info->memory[i];
    if (mem->type != 1) {  // normal ram
      continue;
    }
    uintptr_t addr = mem->base;
    uintptr_t len = mem->length;
    uintptr_t kernel_start = (uintptr_t)boot_info->kernel_base;
    uintptr_t kernel_end = kernel_start + boot_info->kernel_size;
#ifdef LX6
    kprintf("Y1\n");
    addr = ALIGN(addr, PMEM_ALIGN);
    if (len > sizeof(mem_block_t)) {
      len = len & ~(PMEM_ALIGN - 1);
      if (len > sizeof(mem_block_t) + PAGE_SIZE) {
        mem_block_t* block = (mem_block_t*)addr;
        block->addr = (uintptr_t)block + sizeof(mem_block_t);
        block->size = len - sizeof(mem_block_t);
        block->origin_size = block->size;
        block->origin_addr = addr;
        block->type = MEM_FREE;
        block->next = NULL;
        if (mmt.blocks == NULL) {
          mmt.blocks = block;
          mmt.blocks_tail = block;
        } else {
          mmt.blocks_tail->next = block;
          mmt.blocks_tail = block;
        }
      }
    }
    kprintf("Y2\n");
    break;
#else
    if (!(kernel_end < addr || kernel_start > (addr + len))) {
      uintptr_t a1 = addr;
      uintptr_t a2 = addr + len;
      uintptr_t b1 = kernel_start;
      uintptr_t b2 = kernel_end;
      if (b1 > a1) {
        addr = a1;
        len = b1 - a1;
        mm_add_block(addr, len);
      }
      if (b2 < a2) {
        addr = b2;
        len = a2 - b2;
        mm_add_block(addr, len);
      }
    } else {
      mm_add_block(addr, len);
    }
#endif
  }
  kassert(mmt.blocks != NULL);
}

/* 【别在宏末尾带分号】带上分号就只能当语句用，写进表达式会出现
 * "expected ')' before ';'"（相邻合并/切分需要在表达式里算地址）。 */
#define ya_block_ptr(ptr) ((block_t*)(ptr) - 1)
#define ya_block_addr(ptr) ((block_t*)(ptr) + 1)

#define MAGIC_FREE 999999999
#define MAGIC_USED 888888888
#define MAGIC_END 777777777
#define BLOCK_FREE 123456789
#define BLOCK_USED 987654321

static int ya_block_magic_valid(block_t* block) {
  if (block == NULL) {
    return 0;
  }
  return block->magic == MAGIC_USED || block->magic == MAGIC_FREE;
}

/* 【END 槽布局 v2】8 字节 = [MAGIC_END 哨兵(4B)][回指本块头 self 指针(4B)]。
 * self 指针让 ya_phys_prev/ya_phys_next 从 O(n) 链表遍历降为 O(1)：
 * 55 个 fork 子进程收尸时逐张释放几百个页表块，旧实现的每次 kfree 都要
 * 遍历 3 万+ 块的链表两次，TCG 下直接把 futex_wake03 冻死。
 * 所有写 MAGIC_END 的地方一律走 ya_end_write，别处不许手写。 */
static void ya_end_write(block_t* block) {
  int* end = (int*)((u8*)ya_block_addr(block) + block->size);
  end[0] = MAGIC_END;
  end[1] = (int)(u32)(uintptr_t)block;
}

/* O(1) 校验 block 的 END 槽完整（哨兵 + self 回指一致） */
static int ya_end_valid(block_t* block) {
  int* end = (int*)((u8*)ya_block_addr(block) + block->size);
  return end[0] == MAGIC_END && (block_t*)(uintptr_t)(u32)end[1] == block;
}

/* 候选地址必须落在已登记的池内存范围内：phys_next 会向前读一块头的位置，
 * 池尾的块后面可能是未映射区，先挡住再读（O(池数)，池数是个位数）。 */
static int ya_addr_in_pool(void* p) {
  mem_block_t* mb;
  for (mb = mmt.blocks; mb != NULL; mb = mb->next) {
    if ((u8*)p >= (u8*)mb->addr && (u8*)p < (u8*)mb->addr + mb->size) {
      return 1;
    }
  }
  return 0;
}

void ya_heap_stats(u32* free_bytes, u32* free_blocks, u32* max_free_block,
                   u32* alloc_count, u32* last_map);

static ullong sbrk_carved_total = 0;
static ullong sbrk_carve_count = 0;
static ullong ya_free_calls = 0;
static ullong ya_free_bytes = 0;

void* ya_sbrk(size_t size) {
  mem_block_t* current = mmt.blocks;
  kassert(current != NULL);
  void* addr = NULL;
  int found = 0;
  while (current) {
    if (current->type == MEM_FREE) {
      /* 【下溢防护】current->size 是 size_t（无符号）：块剩余不足 4096 时，
       * `current->size - 4096` 会下溢成 0xFFFFFFxx，条件恒为真 —— 于是从一个
       * 只剩几十字节的块里划走 size 字节，current->size 也跟着下溢成巨大值，
       * 此后该块会不断发出与其它分配重叠的内存。必须先确认剩余充足再做减法。 */
      if (current->size > 4096 && size <= (current->size - 4096)) {
        addr = (void*)current->addr;
        current->addr += size;
        current->size -= size;
        if ((current->size - 4096) <= 0) {
          current->type = MEM_USED;
        }
        found = 1;
        break;
      }
    }
    current = current->next;
  }

  sbrk_carved_total += size;
  sbrk_carve_count++;
#if PROBE_TRACE
  if (size >= 65536) {
    static int probe_carve_n = 0;
    if (probe_carve_n < 20) {
      probe_carve_n++;
      log_debug("PROBE sbrk big carve size=%d ra=%x carved=%dM cnt=%d\n",
                (int)size, (u32)(uintptr_t)__builtin_return_address(0),
                (int)(sbrk_carved_total >> 20), (int)sbrk_carve_count);
    }
  }
#endif

  if (!found) {
    /* 所有原始块都放不下：内核堆耗尽（或请求异常巨大）。断言前先把堆画像
     * 打出来 —— 判据见 ya_heap_stats() 的注释：
     *   free_total 大而 max_free_block 小 ⇒ 碎片化（ya_free 的 merge 未实现）
     *   free_total 本身就小                ⇒ 泄漏（free 被静默丢弃/节点丢失） */
    u32 fbytes = 0, fblocks = 0, fmax = 0, acount = 0, lmap = 0;
    ya_heap_stats(&fbytes, &fblocks, &fmax, &acount, &lmap);
    log_error(
        "ya_sbrk oom need=%d | free=%dK free_blocks=%d max_free=%dK "
        "alloc_count=%d last_map=%x carved=%dM cnt=%d\n",
        size, fbytes / 1024, fblocks, fmax / 1024, acount, lmap,
        (int)(sbrk_carved_total >> 20), (int)sbrk_carve_count);
    {
      /* 【已用链画像】carved - freelist - used_live 的差值就是"孤立块"
       * （从两条链上都不可达的块）：判定 ya_free/链操作是否丢块。
       * 【size 直方图】2 的幂分桶：泄漏者的分配尺寸特征一眼可见 */
      block_t* u = mmt.g_block_list;
      ullong used_tot = 0;
      int used_cnt = 0;
      u32 hist[16];
      kmemset(hist, 0, sizeof(hist));
      while (u != NULL) {
        used_tot += u->size;
        used_cnt++;
        int b = 0;
        size_t s = u->size;
        while (((1ull << (b + 1)) <= s) && b < 15) {
          b++;
        }
        hist[b]++;
        u = u->next;
      }
      log_error("  used list: blocks=%d total=%dM\n", used_cnt,
                (int)(used_tot >> 20));
      log_error("  ya_free: calls=%d bytes=%dM (carved=%dM alloc_no=%d)\n",
                (int)ya_free_calls, (int)(ya_free_bytes >> 20),
                (int)(sbrk_carved_total >> 20), (int)mmt.alloc_count);
      {
        extern int g_vmd_calls;
        extern int g_vmd_refskip;
        extern int g_pd_calls;
        log_error("  vmd_calls=%d refskip=%d pd_calls=%d\n", g_vmd_calls,
                  g_vmd_refskip, g_pd_calls);
      }
      probe_leak_dump();
      for (int b = 0; b < 16; b++) {
        if (hist[b] > 0) {
          log_error("    size<=%dK: %d blocks\n", 1 << b, hist[b]);
        }
      }
    }
    for (mem_block_t* b = mmt.blocks; b != NULL; b = b->next) {
      log_error("  raw block %x type=%d size=%d addr=%x\n", b, b->type, b->size,
                b->addr);
    }
    kassert(found > 0);
  }
  kassert(addr != NULL);

  /*
   * Boot only maps a capped window per free block. Past that, map the carved
   * pages only (not a frontier walk across holes). Keeps page_clone cheap so
   * fork/exec under load (infones then gui) does not stall allocating 100+ L2s.
   */
  if (mmt.last_map_addr > 0) {
    uintptr_t need = (uintptr_t)addr + size;
    if (need > mmt.last_map_addr) {
      uintptr_t start = (uintptr_t)addr & ~(PAGE_SIZE - 1);
      uintptr_t end = (need + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
      mmt.extend_phy_count++;
      if ((mmt.extend_phy_count & 0x3f) == 1) {
        kprintf("extend kernel phy map %lx-%lx (count %d)\n", start, end,
                mmt.extend_phy_count);
      }
      for (uintptr_t va = start; va < end; va += PAGE_SIZE) {
        page_map(va, va, PAGE_KMEM);
        page_map_current(va, va, PAGE_KMEM);
      }
      if (end > mmt.last_map_addr) {
        mmt.last_map_addr = end;
      }
    }
  }
  return addr;
}

/* 【PROBE-leak】新 carve 的调用链直方图：OOM 时看谁在持续 carve 不还 */
static struct {
  u32 ra2;
  u32 ra3;
  int cnt;
  ullong bytes;
} s_probe_leak[16];
static int s_probe_leak_n = 0;

static void probe_leak_note(size_t size) {
  if (size < 4096) {
    return;
  }
  u32 r2 = (u32)(uintptr_t)__builtin_return_address(1);
  u32 r3 = (u32)(uintptr_t)__builtin_return_address(2);
  for (int i = 0; i < s_probe_leak_n; i++) {
    if (s_probe_leak[i].ra2 == r2 && s_probe_leak[i].ra3 == r3) {
      s_probe_leak[i].cnt++;
      s_probe_leak[i].bytes += size;
      return;
    }
  }
  if (s_probe_leak_n < 16) {
    s_probe_leak[s_probe_leak_n].ra2 = r2;
    s_probe_leak[s_probe_leak_n].ra3 = r3;
    s_probe_leak[s_probe_leak_n].cnt = 1;
    s_probe_leak[s_probe_leak_n].bytes = size;
    s_probe_leak_n++;
  }
}

void probe_leak_dump(void) {
  for (int i = 0; i < s_probe_leak_n; i++) {
    log_error("  LEAKCAND ra2=%x ra3=%x cnt=%d bytes=%dK\n",
              s_probe_leak[i].ra2, s_probe_leak[i].ra3, s_probe_leak[i].cnt,
              (int)(s_probe_leak[i].bytes >> 10));
  }
}

block_t* ya_new_block(size_t size) {
  probe_leak_note(size);
  block_t* block = ya_sbrk(size + sizeof(block_t) + YA_END_SIZE);
  /* 【对齐铁律】block_t 含 8 字节字段（AArch64）：块头必须 8 字节对齐。
   * 不满足时在 MMU 未开阶段会直接对齐异常（不是能容错的问题），所以断言。 */
  kassert(((uintptr_t)block & 7) == 0);
  block->free = BLOCK_USED;
  block->next = NULL;
  block->prev = NULL;
  block->size = size;
  block->count = 0;
  block->magic = MAGIC_USED;
  if (mmt.g_block_list == NULL) {
    mmt.g_block_list = block;
    mmt.g_block_list_last = block;
  } else {
    mmt.g_block_list_last->next = block;
    block->prev = mmt.g_block_list_last;
    mmt.g_block_list_last = block;
  }

  void* addr = ya_block_addr(block);
  ya_end_write(block);

  return block;
}

/* ---------------- 链操作：唯一入口 ----------------
 * 【为什么必须集中】block->next/prev 被"已用链(g_block_list)"和"空闲链
 * (g_block_free)"共用，任何一处手写摘链/挂链漏改 head/tail 或漏改邻居的
 * prev，都会让块永久孤立（既不在已用链、从空闲链 head 也不可达）——现象就是
 * "跑应用一段时间后内核堆前沿一路推进，最后 ya_sbrk 断言"。
 * 下面的 unlink/append 同时维护 head、last、双向指针，别处一律不许直接改。 */
static void ya_list_unlink(block_t** head, block_t** last, block_t* block) {
  if (block->prev != NULL) {
    block->prev->next = block->next;
  }
  if (block->next != NULL) {
    block->next->prev = block->prev;
  }
  if (*head == block) {
    *head = block->next;
  }
  if (*last == block) {
    *last = block->prev;
  }
  block->prev = NULL;
  block->next = NULL;
}

static void ya_list_append(block_t** head, block_t** last, block_t* block) {
  if (*head == NULL) {
    *head = block;
    *last = block;
    block->prev = NULL;
    block->next = NULL;
    return;
  }
  /* 防御：head 非空而 last 丢了（历史遗留状态），先从头把尾巴找回来，
   * 否则 *last 解引用就是写 NULL+offset（raspi2 上实测崩过）。 */
  if (*last == NULL) {
    block_t* b = *head;
    while (b->next != NULL) {
      b->next->prev = b;
      b = b->next;
    }
    *last = b;
  }
  block->prev = *last;
  block->next = NULL;
  (*last)->next = block;
  *last = block;
}

/* 在两条链里按 header 地址找块：物理相邻合并要按【地址】判断，
 * 而链表是逻辑顺序，所以不能用 next/prev 直接当邻居。 */
static block_t* ya_phys_next(block_t* block) {
  /* 块布局 [header][data(size)][END(8B)] ⇒ 下一块头地址可直接算出 */
  block_t* cand =
      (block_t*)((u8*)ya_block_addr(block) + block->size + YA_END_SIZE);
  /* O(1) 校验三连：池内、magic 合法、END 槽回指自己。
   * 误判需要垃圾数据恰好同时命中 999999999/888888888 的 magic 且
   * END 槽后 4 字节恰为候选头地址，概率可忽略。 */
  if (!ya_addr_in_pool(cand) || !ya_block_magic_valid(cand) ||
      !ya_end_valid(cand)) {
    return NULL;
  }
  return cand;
}

/* block 之前紧邻的物理块（没有则 NULL）：前块的 END 槽恰好落在本块头
 * 之前 8 字节，其 self 回指就是前块头 —— O(1)，替代旧的全链遍历。 */
static block_t* ya_phys_prev(block_t* block) {
  u8* h = (u8*)block;
  int* e = (int*)(h - YA_END_SIZE);
  if (((uintptr_t)e & 3) != 0 || !ya_addr_in_pool(e) || e[0] != MAGIC_END) {
    return NULL;
  }
  block_t* prv = (block_t*)(uintptr_t)(u32)e[1];
  if (prv == NULL || prv == block || !ya_block_magic_valid(prv)) {
    return NULL;
  }
  /* 一致性：prv 的 END 必须就是本块头前的这个槽（防垃圾指针/旧格式） */
  if ((int*)((u8*)ya_block_addr(prv) + prv->size) != e || !ya_end_valid(prv)) {
    return NULL;
  }
  return prv;
}

block_t* ya_find_free_block(size_t size) {
  block_t* block = mmt.g_block_free;
  block_t* prev = NULL;
  block_t* find_block = NULL;
  while (block) {
    if (!(block->magic == MAGIC_USED || block->magic == MAGIC_FREE)) {
      log_error(
          "errro find free block addr %x tid %d,magic error is %d addr %x\n",
          block, block->tid, block->magic, &block->magic);
      /* 【定位信息】只报"链上这个地址 magic 不对"没法判断是谁把链写坏的。
       * 把前驱节点（它的 next 指向坏块）、空闲链头尾、坏块前 8 字节都打出来：
       *   · prev 的 next == block 且 prev 字段正常 ⇒ 是 prev 之后被人写坏；
       *   · block-8 是完整 END 槽（magic=999999999）⇒ 链上挂的是"某块的 END
       *     槽地址"，即某处把 header 指针算成了 +YA_END_SIZE 的偏移地址。 */
      if (prev != NULL) {
        log_error("  prev=%x magic=%x free=%x size=%x next=%x\n", prev,
                  prev->magic, prev->free, prev->size, prev->next);
      }
      log_error("  free head=%x last=%x request=%d\n", mmt.g_block_free,
                mmt.g_block_free_last, (int)size);
      if ((uintptr_t)block >= 0x1000) {
        u32* e = (u32*)((u8*)block - YA_END_SIZE);
        log_error("  block-8: %x %x (END magic=%x)\n", e[0], e[1], MAGIC_END);
      }
      cpu_halt();
      break;
    }
    prev = block;
    if (block->free == BLOCK_FREE && block->size >= size) {
      find_block = block;
      break;
    }
    block = block->next;
  }
  if (find_block == NULL) {
    find_block = ya_new_block(size);
  }
  return find_block;
}

void* ya_alloc(size_t size) {
  if (size <= 0) {
    return NULL;
  }
  size = ALIGN(size, align_to);
  block_t* block;
  /* 【不要用 g_block_list == NULL 当"走新块"的判据】ya_free 会把释放的块从
   * g_block_list 摘到 g_block_free；当所有块都空闲时 g_block_list 恰好为 NULL，
   * 此时本该复用空闲链，却会从原始堆重新 carve 一块新内存 —— 白吃内存。
   * 统一交给 ya_find_free_block：找不到合适块时它自己会 fallback 到
   * ya_new_block（见其函数尾），语义不变但不再白吃内存。 */
  block = ya_find_free_block(size);
  {
    /* 【PROBE-leak3】本次是否走了新 carve（8-16KB）：phy_alloc 检到标记后
     * 打自己的调用链（ya_alloc←mm_alloc←phy_alloc 的 RA 链被尾调用截断） */
    extern volatile int g_probe_carved;
    g_probe_carved = (block->free != BLOCK_FREE && size > 8192 && size <= 16384)
                         ? 1
                         : 0;
  }
  /* 【复用时先把块从空闲链上摘掉】用统一入口，head/last/邻居 prev 一起维护；
   * 以前这里手写摘链漏了 next->prev，空闲链的双向指针会逐渐失真。 */
  if (block->free == BLOCK_FREE) {
    ya_list_unlink(&mmt.g_block_free, &mmt.g_block_free_last, block);
  }

  /* 【大块切分】相邻合并之后，空闲块可能远大于本次请求。若整块给出，这个块
   * 就再也不能服务同尺寸的小请求（下一次又得 carve 新内存），"合并"反而会
   * 加剧碎片。所以余量足够时把尾部切出来还回空闲链，只留 size 给调用方。 */
  if (block->size >= size + sizeof(block_t) + YA_END_SIZE + align_to) {
    block_t* rest = (block_t*)((u8*)ya_block_addr(block) + size + YA_END_SIZE);
    rest->size = block->size - size - sizeof(block_t) - YA_END_SIZE;
    rest->free = BLOCK_FREE;
    rest->count = 0;
    rest->no = 0;
    rest->tid = 0;
    rest->magic = MAGIC_FREE;
    rest->prev = NULL;
    rest->next = NULL;
    ya_end_write(rest);
    ya_list_append(&mmt.g_block_free, &mmt.g_block_free_last, rest);
    block->size = size;
  }

  block->free = BLOCK_USED;
  block->magic = MAGIC_USED;
  void* addr = ya_block_addr(block);
  kassert(addr != NULL);
  /* 数据区末尾的越界哨兵：切分时就地写过，未切分时是建块时写的；
   * 这里统一再写一次（覆盖"合并后残留的中间 header"）。 */
  ya_end_write(block);

  block->no = mmt.alloc_count++;
  mmt.alloc_size += size;
  {
    /* 【PROBE-leak2】8-16KB 分配采样：每 512 次打一次 RA 链。
     * 直方图桶 b=13 表示 size∈[8192,16384) 字节（打印的 K 单位是错的），
     * 122569 块都在这桶 ⇒ 泄漏者分配的是 8-16KB。 */
#if PROBE_TRACE
    if (size > 8192 && size <= 16384 && (mmt.alloc_count & 0x1FF) == 0) {
      static int probe_l2_n = 0;
      if (probe_l2_n < 10) {
        probe_l2_n++;
        log_error(
            "PROBE alloc4-8k no=%d ra1=%x ra2=%x ra3=%x ra4=%x\n",
            (int)mmt.alloc_count, (u32)(uintptr_t)__builtin_return_address(0),
            (u32)(uintptr_t)__builtin_return_address(1),
            (u32)(uintptr_t)__builtin_return_address(2),
            (u32)(uintptr_t)__builtin_return_address(3));
      }
    }
#endif
  }

#ifdef DEBUG
#include "kernel/thread.h"
  thread_t* t = thread_current();
  if (t != NULL) {
    if (t != NULL) {
      block->tid = t->id;
    } else {
      block->tid = 66666;
    }

    kprintf(
        "tid %d alloc %x size=%d count=%d total=%dk  baddr=%x bsize=%d "
        "bcount=%d "
        "last "
        "map=%x\n",
        t->id, addr, size, mmt.alloc_count, mmt.alloc_size / 1024, block,
        block->size, block->count, mmt.last_map_addr);
    ya_verify();
    // kprintf("ya_alloc(%d);//no %d addr %x \n", size, block->no, addr);
  }

#endif

  return addr;
}

void ya_verify() {
  block_t* current = mmt.g_block_list;
  int total = 0;
  int free = 0;
  int used = 0;
  while (current) {
    kassert(current->size > 0);
    if (current->free == BLOCK_USED) {
      kassert(current->magic == MAGIC_USED);
      used += current->size;
    } else if (current->free == BLOCK_FREE) {
      kassert(current->magic == MAGIC_FREE);
      free += current->size;
    } else {
      if (current->tid > 0) {
        kprintf("tid %d block error addr %x free %d\n", current->tid,
                &current->free, current->free);
        kassert((current->free == BLOCK_FREE || current->free == BLOCK_USED));
      }
    }
    void* addr = ya_block_addr(current);
    kassert(addr != NULL);
    /* END 槽 v2：校验哨兵 + self 回指（不再自我重写——重写会把真问题盖掉） */
    kassert(ya_end_valid(current));

    current = current->next;
    total++;
  }

  current = mmt.g_block_free;
  while (current) {
    kassert(current->size > 0);
    if (current->free == BLOCK_USED) {
      kassert(current->magic == MAGIC_USED);
      used += current->size;
    } else if (current->free == BLOCK_FREE) {
      kassert(current->magic == MAGIC_FREE);
      free += current->size;
    } else {
      kprintf("tid %d free block error addr %x free %d\n", current->tid,
              &current->free, current->free);
      kassert((current->free == BLOCK_FREE || current->free == BLOCK_USED));
    }
    void* addr = ya_block_addr(current);
    kassert(addr != NULL);
    kassert(ya_end_valid(current));

    current = current->next;
    total++;
  }

  kprintf("-------------------------------\n");
  kprintf("verify total %d free %dk used %dk\n", total, free / 1024,
          used / 1024, mmt);
  mem_block_t* block = mmt.blocks;
  while (block) {
    kprintf("block:%x type:%d size:%d start: %x end:%x\n", block, block->type,
            block->size, block->addr, block->addr + block->size);
    block = block->next;
  }
}

/* 堆画像（供 ya_sbrk 的 OOM 报错路径使用）：空闲链总量/块数/最大块 +
 * 分配计数 + 映射前沿。
 * 判据：若 free_total 很大（几十 MB）而 max_free_block 只有几 KB，说明空闲块
 * 高度碎片化、无法满足稍大的请求 ⇒ 分配器只能不断 carve 新块（前沿一直推进）。 */
void ya_heap_stats(u32* free_bytes, u32* free_blocks, u32* max_free_block,
                   u32* alloc_count, u32* last_map) {
  block_t* b = mmt.g_block_free;
  u32 tot = 0, cnt = 0, mx = 0;
  while (b != NULL) {
    tot += b->size;
    cnt++;
    if (b->size > mx) {
      mx = b->size;
    }
    b = b->next;
  }
  if (free_bytes) *free_bytes = tot;
  if (free_blocks) *free_blocks = cnt;
  if (max_free_block) *max_free_block = mx;
  if (alloc_count) *alloc_count = mmt.alloc_count;
  if (last_map) *last_map = (u32)mmt.last_map_addr;
}

void ya_free(void* ptr) {
  if (ptr == NULL) {
    return;
  }
  ya_free_calls++;
  block_t* block = ya_block_ptr(ptr);
#ifdef DEBUG
  // kprintf("ya_free_no(%d);\n", block->no);
  kprintf("free  %x size=%d baddr=%x bsize=%d bcount=%d\n", ptr, block->size,
          block, block->size, block->count);
#endif
  if (!ya_block_magic_valid(block)) {
    log_error("ya_free invalid block ptr=%x block=%x magic=%x\n", ptr, block,
              block != NULL ? block->magic : 0);
    return;
  }
  if (block->free != BLOCK_USED || block->magic != MAGIC_USED || block->size <= 0) {
    log_error("ya_free bad state ptr=%x block=%x free=%x magic=%x size=%x\n", ptr,
              block, block->free, block->magic, block->size);
    return;
  }

  int* end = ptr + block->size;
  if ((*end) != MAGIC_END) {
    log_error("ya_free end marker corrupted ptr=%x block=%x end=%x expect=%x\n",
              ptr, block, *end, MAGIC_END);
    return;
  }

  block->magic = MAGIC_FREE;
  block->free = BLOCK_FREE;
  block->count = 0;
  kmemset(ptr, 0, block->size);

  /* 【摘出已用链】必须走统一入口。原代码只更新了 g_block_list，漏了
   * g_block_list_last —— 一旦释放的是表尾，ya_new_block 仍会往这个"已经挂到
   * 空闲链上"的块后面接（把它的 next 写坏），空闲链就此断掉，节点永久孤立。 */
  ya_list_unlink(&mmt.g_block_list, &mmt.g_block_list_last, block);

  /* 【向后合并】把紧跟在后面的空闲块并进来（原先 merge 整段被注释掉）。
   * 块布局 [header][data(size)][END] ⇒ 跨过下一个块 = sizeof(block_t)+its
   * size+YA_END_SIZE。合并后数据区变大，末尾哨兵仍是原邻居的 END。 */
  for (;;) {
    block_t* nxt = ya_phys_next(block);
    if (nxt == NULL || nxt->free != BLOCK_FREE) {
      break;
    }
    ya_list_unlink(&mmt.g_block_free, &mmt.g_block_free_last, nxt);
    block->size += sizeof(block_t) + nxt->size + YA_END_SIZE;
    /* 被吸收的 header 作废：magic 置 0 ⇒ 以后误释放它会被 ya_free 的
     * "invalid block" 分支挡下（清晰报错），而不是把堆链改坏。 */
    nxt->magic = 0;
    nxt->free = 0;
    nxt->size = 0;
    nxt->prev = NULL;
    nxt->next = NULL;
  }

  /* 【向前合并】如果前一个物理块也空闲，就把当前块并进它（结果块=前块） */
  for (;;) {
    block_t* prv = ya_phys_prev(block);
    if (prv == NULL || prv->free != BLOCK_FREE) {
      break;
    }
    ya_list_unlink(&mmt.g_block_free, &mmt.g_block_free_last, prv);
    prv->size += sizeof(block_t) + block->size + YA_END_SIZE;
    block->magic = 0;
    block->free = 0;
    block->size = 0;
    block->prev = NULL;
    block->next = NULL;
    block = prv;
  }

  /* 合并后的数据区末尾必须是完整 END 槽（原块的 END 正好落在新末尾，
   * 但 self 回指要更新成合并后的结果块） */
  ya_end_write(block);
  ya_free_bytes += block->size;

  /* 挂回空闲链（append 统一维护 head/last/双向指针） */
  ya_list_append(&mmt.g_block_free, &mmt.g_block_free_last, block);
  ptr = NULL;
}

int is_line_intersect(int a1, int a2, int b1, int b2) {
  return !(b2 < a1 || b1 > a2);
}


void mm_add_block(uintptr_t addr, uintptr_t len) {
  mem_block_t* block = (mem_block_t*)addr;
  /* 【必须与恒等映射同上限】超出的部分没有身份映射；一旦发出去，内核按物理地址
   * 直访（COW 破写 / fork 深拷贝的 kmemcpy((void*)pa,…)）会在内核里再触发一次
   * 翻译故障 ⇒ 不收敛 ⇒ 缺页风暴。与 mm_parse_map 的 MM_IDENTITY_MAX 保持一致
   * （实测不加：池到 0x3f000000，而映射只到 0x5017000 ✗）。 */
  if (MM_IDENTITY_MAX != 0 && len > MM_IDENTITY_MAX) {
    len = MM_IDENTITY_MAX;
  }
  block->addr = (uintptr_t)block + sizeof(mem_block_t);
  block->size = len - sizeof(mem_block_t);
  block->origin_size = block->size;
  block->origin_addr = addr;
  block->type = MEM_FREE;
  block->next = NULL;
  if (mmt.blocks == NULL) {
    mmt.blocks = block;
    mmt.blocks_tail = block;
  } else {
    mmt.blocks_tail->next = block;
    mmt.blocks_tail = block;
  }
  kprintf("add block:%x type:%d size:%d start: %x end:%x\n", block, block->type,
          block->size, block->addr, block->addr + block->size);
}

void mm_dump_phy() {
#ifdef LX6
  kprintf("lx6 phy skip\n");
  return;
#endif
  for (int i = 0; i < boot_info->memory_number; i++) {
    memory_sinfo_t* m = (memory_sinfo_t*)&boot_info->memory[i];
    kprintf("base:%x %x lenght:%x %x type:%d\n", m->baseh, m->basel, m->lengthh,
            m->lengthl, m->type);
  }
  kprintf("total memory %dm %dk\n", boot_info->total_memory / 1024 / 1024,
          boot_info->total_memory / 1024);
}

size_t ya_real_size(void* ptr) {
  if (ptr == NULL) {
    return 0;
  }
  block_t* block = ya_block_ptr(ptr);
  if (!ya_block_magic_valid(block)) {
    log_error("ya_real_size invalid block ptr=%x block=%x magic=%x\n", ptr, block,
              block != NULL ? block->magic : 0);
    return 0;
  }
  return block->size;
}

void mm_init() {
  kprintf("mm init\n");
  mmt.init = ya_alloc_init;
  mmt.alloc = ya_alloc;
  mmt.free = ya_free;
  mmt.size = ya_real_size;
  mmt.blocks = NULL;
  mmt.blocks_tail = NULL;
  mmt.g_block_list = NULL;
  mmt.g_block_list_last = NULL;
  mmt.g_block_free = NULL;
  mmt.g_block_free_last = NULL;
  mmt.alloc_count = 0;
  mmt.alloc_size = 0;
  mmt.last_map_addr = 0;
  mmt.extend_phy_count = 0;

  count = 0;
#ifdef LX6
  kprintf("M0\n");
#else
  kprintf("phy dump\n");
#endif
  mm_dump_phy();

#ifdef LX6
  kprintf("M1\n");
#else
  kprintf("mm init default\n");
#endif
  mm_init_default();

#ifdef LX6
  kprintf("M2\n");
#else
  kprintf("alloc init\n");
#endif

  // mm init
  mmt.init();
#ifdef LX6
  kprintf("M3\n");
#endif
}

size_t mm_get_size(void* addr) { return mmt.size(addr); }

size_t mm_get_align_size(void* addr) { return mmt.size(((void**)addr)[-1]); }

void* mm_alloc(size_t size) {
  void* p = mmt.alloc(size);
  if (p == 0x23e000) {
    int i = 0;
  }
  if (p == NULL) {
    return NULL;
  }
  kmemset(p, 0, size);
  return p;
}

void mm_free(void* ptr) { return mmt.free(ptr); }

void* mm_alloc_zero_align(size_t size, u32 alignment) {
  void* p1;   // original block
  void** p2;  // aligned block
  int offset = alignment - 1 + sizeof(void*);
  if ((p1 = (void*)mm_alloc(size + offset)) == NULL) return NULL;
  p2 = (void**)(((size_t)(p1) + offset) & ~(alignment - 1));
  p2[-1] = p1;
  kmemset(p2, 0, size);
#ifdef DEBUG
  kprintf("alloc align %x size=%d\n", p2, size);
#endif
  return p2;
}

void mm_free_align(void* addr) {
#ifdef DEBUG
  kprintf("free align %x\n", addr);
#endif
  mm_free(((void**)addr)[-1]);
}

/* 见 arch/pmemory.h 声明。 */
int mm_page_in_ram(void* p) {
  unsigned long a = (unsigned long)p;
  if (a == 0 || (a & (PAGE_SIZE - 1)) != 0) {
    return 0;
  }
  for (int i = 0; i < boot_info->memory_number; i++) {
    if (boot_info->memory[i].type != 1) {
      continue;
    }
    unsigned long s = (unsigned long)boot_info->memory[i].base;
    unsigned long e = s + boot_info->memory[i].length;
    if (a >= s && a + PAGE_SIZE <= e) {
      return 1;
    }
  }
  return 0;
}

/* 设备保留区间（显示缓冲）**不在这里处理**：显存段已由 boot 侧的内存配置
 * （boot/arm/init-armv5.c 的 VERSATILEPB 条目）从 RAM 里排除，分配器根本
 * 看不到它，所以不需要任何运行时"保留区间"过滤。 */
/* 【内核堆水位线：页分配不许把 raw 区吃穿】mm_alloc_page（用户物理页）与
 * ya_sbrk（内核字节堆）共用 mmt.blocks；页释放只进 mm_page_free_list、
 * 不还 mmt.blocks ⇒ fork/深拷贝密集的负载（LTP 全量，实测 60 个测试）会把
 * 整段 RAM 切成页，内核堆被饿死（ya_sbrk assert found>0，整机停摆）。
 *
 * 通用算法（不挑平台/负载）：raw 区剩余量（MEM_FREE 总和）低于水位线时，
 * 页分配一律失败，让用户态优雅拿到 NULL；水位线按总内存等比例伸缩——
 *   watermark = clamp(total/8, 4MB, 128MB)
 * 【判据语义】mm_get_free() 是 raw 区剩余，随"历史累计发页量"单调下降是
 * 正常的（退回的页在 mm_page_free_list 循环复用，不占 raw）。水位线覆盖的
 * 是【内核字节堆的 raw 足迹 = 累计分配量】：碎片导致 freed 块很少拼回大块，
 * raw 只出不进，足迹随测试数缓慢增长（实测 60 测试 ~13MB，含 xwin 9.4MB）。
 * 预留的 raw 对页池不是浪费——页的复用走 free list，不经过 mmt.blocks。 */
static ullong mm_kernel_heap_watermark(void) {
  ullong wm = mm_get_total() / 8;
  if (wm < 4ull * 1024 * 1024) {
    wm = 4ull * 1024 * 1024;
  }
  if (wm > 128ull * 1024 * 1024) {
    wm = 128ull * 1024 * 1024;
  }
  return wm;
}

void* mm_alloc_page(void) {
  if (mm_page_free_list != NULL) {
    void* page = mm_page_free_list;
    mm_page_free_list = *(void**)page;
    return page;
  }

  /* 【水位线闸门】raw 区剩余低于水位线就不再发页：这点内存是内核字节堆
   * （ya_sbrk）的保命粮。用户态拿到 NULL 后走分配失败路径（musl malloc
   * 返回 NULL / 测试 TBROK），内核不会停摆。 */
  if (mm_get_free() < mm_kernel_heap_watermark()) {
    return NULL;
  }

  mem_block_t* current = mmt.blocks;
  while (current != NULL) {
    if (current->type == MEM_FREE) {
      uintptr_t aligned = ALIGN(current->addr, PAGE_SIZE);
      size_t padding = aligned - current->addr;
      size_t need = padding + PAGE_SIZE;
      if (need <= current->size) {
        current->addr = aligned + PAGE_SIZE;
        current->size -= need;
        if (current->size <= PAGE_SIZE) {
          current->type = MEM_USED;
        }
        return (void*)aligned;
      }
    }
    current = current->next;
  }
  return NULL;
}

void mm_free_page(void* p) {
  if (p == NULL) {
    return;
  }
  *(void**)p = mm_page_free_list;
  mm_page_free_list = p;
}

ullong mm_get_total() {
  ullong total = 0;
  mem_block_t* p = mmt.blocks;
  for (; p != NULL; p = p->next) {
    total += p->origin_size;
  }
  return total;
}

ullong mm_get_free() {
  ullong free = 0;
  mem_block_t* p = mmt.blocks;
  for (; p != NULL; p = p->next) {
    if ((p->type == MEM_FREE)) {
      free += p->size;
    }
  }
  return free;
}

#else

mem_block_t* block_alloc_head = NULL;
mem_block_t* block_alloc_tail = NULL;

void mm_dump_phy() {
  for (int i = 0; i < boot_info->memory_number; i++) {
    memory_sinfo_t* m = (memory_sinfo_t*)&boot_info->memory[i];
    kprintf("base:%x %x lenght:%x %x type:%d\n", m->baseh, m->basel, m->lengthh,
            m->lengthl, m->type);
  }
  kprintf("total memory %dm %dk\n", boot_info->total_memory / 1024 / 1024,
          boot_info->total_memory / 1024);
}

void mm_init() {
  kprintf("mm init\n");
  mmt.blocks = NULL;
  block_alloc_head = NULL;
  block_alloc_tail = NULL;
  mmt.blocks_tail = NULL;
  count = 0;

  mmt.blocks = NULL;

  kprintf("phy dump\n");
  mm_dump_phy();
  kprintf("alloc init\n");
  // mm init
  mm_alloc_init();

  kprintf("mm init default\n");
  mm_init_default();
}

int is_line_intersect(int a1, int a2, int b1, int b2) {
  return !(b2 < a1 || b1 > a2);
}

void mm_add_block(uintptr_t addr, uintptr_t len) {
#ifdef LX6
  kprintf("B0\n");
#endif
  addr = ALIGN(addr, MEMORY_ALIGMENT);
  if (len <= sizeof(mem_block_t)) {
    return;
  }
  len = len & ~(MEMORY_ALIGMENT - 1);
  if (len <= sizeof(mem_block_t) + PAGE_SIZE) {
    return;
  }
  mem_block_t* block = (mem_block_t*)addr;
#ifdef LX6
  kprintf("B1\n");
#endif
  block->addr = (uintptr_t)block + sizeof(mem_block_t);
  block->size = len - sizeof(mem_block_t);
  block->origin_size = block->size;
  block->origin_addr = addr;
  block->type = MEM_FREE;
  block->next = NULL;
#ifdef LX6
  kprintf("B2\n");
#endif
  if (mmt.blocks == NULL) {
    mmt.blocks = block;
    mmt.blocks_tail = block;
  } else {
    mmt.blocks_tail->next = block;
    mmt.blocks_tail = block;
  }
  kprintf("block:%x type:%d size:%d start: %x end:%x\n", block, block->type,
          block->size, block->addr, block->addr + block->size);
}

void mm_alloc_init() {
  memory_info_t* first_mem = (memory_info_t*)&boot_info->memory[0];
  u32 size = sizeof(mem_block_t) * boot_info->memory_number;
  u32 pos = 0;
  for (int i = 0; i < boot_info->memory_number; i++) {
    memory_info_t* mem = (memory_info_t*)&boot_info->memory[i];
    if (mem->type != 1) {  // normal ram
      continue;
    }
    // skip
    u32 addr = mem->base;
    u32 len = mem->length;
    // mm_add_block(addr, len);
    u32 kernel_start = boot_info->kernel_base;
    u32 kernel_end = kernel_start + boot_info->kernel_size;
    if (is_line_intersect(addr, addr + len, kernel_start, kernel_end)) {
      int a1 = addr;
      int a2 = addr + len;
      int b1 = kernel_start;
      int b2 = kernel_end;
      if (b1 > a1) {
        addr = a1;
        len = b1 - a1;
        mm_add_block(addr, len);
      }
      if (b2 < a2) {
        addr = b2;
        len = a2 - b2;
        mm_add_block(addr, len);
      }
    } else {
      mm_add_block(addr, len);
    }
  }
}

#define debug

void* mm_alloc(size_t size) {
  mem_block_t* p = mmt.blocks;
  debug("malloc count %d size %d\n", count, size);
  size_t pre_alloc_size = size + sizeof(mem_block_t);
  pre_alloc_size = (pre_alloc_size + 15) & ~0xF;  // 16-byte alignment for ARM64
  for (; p != NULL; p = p->next) {
    debug("p=>:%x type:%d size:%x\n", p, p->type, p->size);
    if ((p->type != MEM_FREE)) {
      continue;
    }
    // debug("p2=>:%x type:%d size:%x\n", p, p->type, p->size);
    if ((pre_alloc_size) <= p->size) {
      // debug("p:%x pre_alloc_size:%d size:%d
      // type:%d\n",p,pre_alloc_size,p->size,p->type);
      mem_block_t* new_block = (mem_block_t*)p->addr;
      if (new_block == NULL) continue;
      p->addr += pre_alloc_size;
      p->size -= pre_alloc_size;
      new_block->addr = (uintptr_t)new_block + sizeof(mem_block_t);
      new_block->size = size;
      new_block->next = NULL;
      new_block->type = MEM_USED;

      if (block_alloc_head == NULL) {
        block_alloc_head = new_block;
        block_alloc_tail = new_block;
      } else {
        block_alloc_tail->next = new_block;
        block_alloc_tail = new_block;
      }
      count++;
      // kprintf("alloc count:%d: addr:%x size:%d\n", count,
      // new_block->addr,new_block->size);
      if (new_block->addr == 0) {
        mm_dump();
      }
      // cpu_backtrace();
      // mm_dump_print(block_available);
      return (void*)new_block->addr;
    }
  }
  kprintf("erro alloc count %d size %d kb\n", count, size / 1024);
  mm_dump();
  for (;;);

  return NULL;
}

void* mm_alloc_zero_align(size_t size, u32 alignment) {
  void *p1, *p2;
  if ((p1 = (void*)mm_alloc(size + alignment + sizeof(size_t))) == NULL) {
    return NULL;
  }
  size_t addr = (size_t)p1 + alignment + sizeof(size_t);
  p2 = (void*)(addr - (addr % alignment));
  *((size_t*)p2 - 1) = (size_t)p1;
  kmemset(p2, 0, size);
  return p2;

  // int offset = alignment + sizeof(void*);
  // void* addr = mm_alloc(size + offset);
  // addr+=1;
  // memset(addr, 0, size + offset);
  // void** align = (void**)(((size_t)addr + offset-1) & ~(alignment - 1));
  // align[-1] = addr;
  // return align;

  // size_t request_size = size + alignment;
  // char* buf = (char*)mm_alloc(request_size);
  // memset(buf, 0, request_size);
  // size_t remainder = ((size_t)buf) % alignment;
  // size_t offset = alignment - remainder;
  // char* ret = buf + (unsigned char)offset;
  // *(unsigned char*)(ret - 1) = offset;
  // return (void*)ret;
}

void mm_free_align(void* addr) {
  if (addr) {
    void* real = ((void**)addr)[-1];
    mm_free(real);
  }
  // int offset = *(((char*)addr) - 1);
  // mm_free(((char*)addr) - offset);
}

void mm_dump_print(mem_block_t* p) {
  u32 use = 0;
  u32 free = 0;
  for (; p != NULL; p = p->next) {
    if ((p->type == MEM_FREE)) {
      kprintf("free %x %d\n", p->addr, p->size);
      free += p->size;
    } else {
      kprintf("use %x %d\n", p->addr, p->size);
      use += p->size;
    }
  }
  kprintf("total ");
  if (use >= 0) {
    kprintf(" use: %dkb %dmb", use / 1024, use / 1024 / 1024);
  }
  if (free >= 0) {
    kprintf(" free: %dkb %dmb", free / 1024, free / 1024 / 1024);
  }
  kprintf("\n");
}

void mm_dump() {
  kprintf("dump memory\n");
  kprintf("---dump alloc-------\n");
  mm_dump_print(block_alloc_head);

  kprintf("---dump available---\n");
  mm_dump_print(mmt.blocks);
  kprintf("dump end\n\n");
}

ullong mm_get_total() {
  ullong total = 0;
  mem_block_t* p = mmt.blocks;
  for (; p != NULL; p = p->next) {
    total += p->size;
  }
  return total;
}

ullong mm_get_free() {
  ullong free = 0;
  mem_block_t* p = block_alloc_head;
  for (; p != NULL; p = p->next) {
    if ((p->type == MEM_FREE)) {
      free += p->size;
    }
  }
  return free;
}

size_t mm_get_size(void* addr) {
  if (addr == NULL) return 0;
  mem_block_t* block = (mem_block_t*)((uintptr_t)addr - sizeof(mem_block_t));
  return block->size;
}

void mm_free(void* addr) {
  debug("free %x\n", addr);
  if (addr == NULL) return;
  mem_block_t* block = (mem_block_t*)((uintptr_t)addr);
  if (block->addr == 0) {
    kprintf("mm free error %x\n", addr);
    return;
  }
  block->next = NULL;
  block->type = MEM_FREE;
  mmt.blocks_tail->next = block;
  mmt.blocks_tail = block;
}

u32 mm_get_block_size(void* addr) {
  mem_block_t* p = block_alloc_head;
  for (; p != NULL; p = p->next) {
    if (p->addr == addr) {
      return p->size;
    }
  }
}
#endif

void map_mem_block(void* page, vaddr_t max_size, u64 flags) {
  mem_block_t* p = mmt.blocks;
  for (; p != NULL; p = p->next) {
    vaddr_t address = p->origin_addr;
    /* Map this block only — never past origin_size into a hole/device. */
    vaddr_t size = p->origin_size + sizeof(mem_block_t);
    if (max_size != 0 && size > max_size) {
      size = max_size;
    }
    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    page_map_range(page, address, address, size, flags);
    kprintf("map mem block addr range %lx - %lx\n", address, address + size);
    if (address + size > mmt.last_map_addr) {
      mmt.last_map_addr = address + size;
    }
  }
}

void page_map_range(void* page, vaddr_t vaddr, vaddr_t paddr, vaddr_t size, u64 flag) {
  int pages = size / PAGE_SIZE + (size % PAGE_SIZE > 0 ? 1 : 0);
  for (int j = 0; j < pages; j++) {
    page_map_on((u64*)page, vaddr, paddr, flag);
    vaddr += PAGE_SIZE;
    paddr += PAGE_SIZE;
  }
}

void page_map_kernel(void* page, u64 flag_x, u64 flag_rw) {
  vaddr_t address = 0;
  kprintf("map kernel start\n");
  for (int i = 0; i < boot_info->segments_number; i++) {
    vaddr_t size = boot_info->segments[i].size;
    address = (vaddr_t)(uintptr_t)boot_info->segments[i].start;
    u64 flag = flag_x;
    u32 type = boot_info->segments[i].type;
    if (type == 2) {
      flag = flag_rw;
    }
    page_map_range(page, address, address, size, flag);

    kassert(address != 0);
    kassert(size != 0);
    kprintf("map kernel %d range %lx  - %lx type %d\n", i, address,
            address + size, type);
  }
  kprintf("map kernel end %d\n", boot_info->segments_number);
}

void mm_parse_map(void* kernel_page_dir) {
  kprintf("map mem block start\n");
  /* 恒等映射窗口 = MM_IDENTITY_MAX（默认 0 = 全 RAM）。历史上限 80MB 是为
   * 了避免 fork 复制整段恒等映射的 L2 表，现在 page_copy 对 < EXEC_ADDR 的
   * 恒等区 L2 是共享的（见各 arch mm.c），全量映射不再有每 fork 开销。
   * PAGE_KERNEL 与 valloc/user 的属性一致（ARMv7 同物理页属性必须相同）。 */
  map_mem_block(kernel_page_dir, MM_IDENTITY_MAX, PAGE_KERNEL);

  int size = PAGE_SIZE * 200;
  kprintf("map mem range %x %x\n", 0, size);

  kprintf("map mem kernel\n");
  page_map_kernel(kernel_page_dir, PAGE_KERNEL, PAGE_KMEM);

  platform_map();
}