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
#define MM_IDENTITY_MAX (PAGE_SIZE * 20000)
#endif

#include "kernel/common.h"
#include "kernel/logger.h"
#include "kernel/page.h"
#include "kernel/string.h"

static u32 count = 0;
const size_t align_to = 16;
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

void ya_heap_stats(u32* free_bytes, u32* free_blocks, u32* max_free_block,
                   u32* alloc_count, u32* last_map);

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

  if (!found) {
    /* 所有原始块都放不下：内核堆耗尽（或请求异常巨大）。断言前先把堆画像
     * 打出来 —— 判据见 ya_heap_stats() 的注释：
     *   free_total 大而 max_free_block 小 ⇒ 碎片化（ya_free 的 merge 未实现）
     *   free_total 本身就小                ⇒ 泄漏（free 被静默丢弃/节点丢失） */
    u32 fbytes = 0, fblocks = 0, fmax = 0, acount = 0, lmap = 0;
    ya_heap_stats(&fbytes, &fblocks, &fmax, &acount, &lmap);
    log_error(
        "ya_sbrk oom need=%d | free=%dK free_blocks=%d max_free=%dK "
        "alloc_count=%d last_map=%x\n",
        size, fbytes / 1024, fblocks, fmax / 1024, acount, lmap);
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

block_t* ya_new_block(size_t size) {
  block_t* block = ya_sbrk(size + sizeof(block_t) + +sizeof(int));
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
  int* end = addr + block->size;
  *end = MAGIC_END;  // check overflow

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
static block_t* ya_find_block_by_header(block_t* hdr) {
  block_t* b;
  if (hdr == NULL) {
    return NULL;
  }
  for (b = mmt.g_block_list; b != NULL; b = b->next) {
    if (b == hdr) {
      return b;
    }
  }
  for (b = mmt.g_block_free; b != NULL; b = b->next) {
    if (b == hdr) {
      return b;
    }
  }
  return NULL;
}

/* block 之后紧邻的物理块（没有则 NULL）。
 * 块布局：[block_t header][data(size)][int MAGIC_END]
 * ⇒ 下一个 header 地址 = data + size + sizeof(int)。
 * 必须用链表核对这个地址确实是登记过的块，否则会把越界地址当块用。 */
static block_t* ya_phys_next(block_t* block) {
  return ya_find_block_by_header(
      (block_t*)((u8*)ya_block_addr(block) + block->size + sizeof(int)));
}

/* block 之前紧邻的物理块（没有则 NULL） */
static block_t* ya_phys_prev(block_t* block) {
  block_t* lists[2];
  int li;
  lists[0] = mmt.g_block_list;
  lists[1] = mmt.g_block_free;
  for (li = 0; li < 2; li++) {
    for (block_t* b = lists[li]; b != NULL; b = b->next) {
      if (b == block) {
        continue;
      }
      if ((block_t*)((u8*)ya_block_addr(b) + b->size + sizeof(int)) == block) {
        return b;
      }
    }
  }
  return NULL;
}

block_t* ya_find_free_block(size_t size) {
  block_t* block = mmt.g_block_free;
  block_t* find_block = NULL;
  while (block) {
    if (!(block->magic == MAGIC_USED || block->magic == MAGIC_FREE)) {
      log_error(
          "errro find free block addr %x tid %d,magic error is %d addr %x\n",
          block, block->tid, block->magic, &block->magic);
      cpu_halt();
      break;
    }
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
  /* 【复用时先把块从空闲链上摘掉】用统一入口，head/last/邻居 prev 一起维护；
   * 以前这里手写摘链漏了 next->prev，空闲链的双向指针会逐渐失真。 */
  if (block->free == BLOCK_FREE) {
    ya_list_unlink(&mmt.g_block_free, &mmt.g_block_free_last, block);
  }

  /* 【大块切分】相邻合并之后，空闲块可能远大于本次请求。若整块给出，这个块
   * 就再也不能服务同尺寸的小请求（下一次又得 carve 新内存），"合并"反而会
   * 加剧碎片。所以余量足够时把尾部切出来还回空闲链，只留 size 给调用方。 */
  if (block->size >= size + sizeof(block_t) + sizeof(int) + align_to) {
    block_t* rest = (block_t*)((u8*)ya_block_addr(block) + size + sizeof(int));
    rest->size = block->size - size - sizeof(block_t) - sizeof(int);
    rest->free = BLOCK_FREE;
    rest->count = 0;
    rest->no = 0;
    rest->tid = 0;
    rest->magic = MAGIC_FREE;
    rest->prev = NULL;
    rest->next = NULL;
    *((int*)((u8*)ya_block_addr(rest) + rest->size)) = MAGIC_END;
    ya_list_append(&mmt.g_block_free, &mmt.g_block_free_last, rest);
    block->size = size;
  }

  block->free = BLOCK_USED;
  block->magic = MAGIC_USED;
  void* addr = ya_block_addr(block);
  kassert(addr != NULL);
  /* 数据区末尾的越界哨兵：切分时就地写过，未切分时是建块时写的；
   * 这里统一再写一次（覆盖"合并后残留的中间 header"）。 */
  *((int*)((u8*)addr + block->size)) = MAGIC_END;

  block->no = mmt.alloc_count++;
  mmt.alloc_size += size;

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
    int* end = addr + current->size;
    *end = MAGIC_END;  // check overflow
    kassert((*end) == MAGIC_END);

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
    int* end = addr + current->size;
    *end = MAGIC_END;  // check overflow
    kassert((*end) == MAGIC_END);

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
   * 块布局 [header][data(size)][int END] ⇒ 跨过下一个块 = sizeof(block_t)+its
   * size+sizeof(int)。合并后数据区变大，末尾哨兵仍是原邻居的 END。 */
  for (;;) {
    block_t* nxt = ya_phys_next(block);
    if (nxt == NULL || nxt->free != BLOCK_FREE) {
      break;
    }
    ya_list_unlink(&mmt.g_block_free, &mmt.g_block_free_last, nxt);
    block->size += sizeof(block_t) + nxt->size + sizeof(int);
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
    prv->size += sizeof(block_t) + block->size + sizeof(int);
    block->magic = 0;
    block->free = 0;
    block->size = 0;
    block->prev = NULL;
    block->next = NULL;
    block = prv;
  }

  /* 合并后的数据区末尾必须是 MAGIC_END（原块的 END 正好落在新末尾） */
  *((int*)((u8*)ya_block_addr(block) + block->size)) = MAGIC_END;

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
  if (len > MM_IDENTITY_MAX) {
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
void* mm_alloc_page(void) {
  if (mm_page_free_list != NULL) {
    void* page = mm_page_free_list;
    mm_page_free_list = *(void**)page;
    return page;
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
  /* Cap ~80MB/block. Full 128MB identity PTEs make every fork page_clone
   * allocate 100+ L2 tables — hangs when starting gui while infones runs.
   * PAGE_KERNEL 与 valloc/user 的属性一致（ARMv7 同物理页属性必须相同）。 */
  map_mem_block(kernel_page_dir, MM_IDENTITY_MAX, PAGE_KERNEL);

  int size = PAGE_SIZE * 200;
  kprintf("map mem range %x %x\n", 0, size);

  kprintf("map mem kernel\n");
  page_map_kernel(kernel_page_dir, PAGE_KERNEL, PAGE_KMEM);

  platform_map();
}