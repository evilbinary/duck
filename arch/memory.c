#include "memory.h"

#include "kernel/common.h"

static u32 count = 0;
const size_t align_to = 16;
extern boot_info_t* boot_info;

#define ALIGN(x, a) (x + (a - 1)) & ~(a - 1)

memory_manager_t mmt;

// #define DEBUG 1
#define MM_YA_ALLOC 1

#ifdef MM_YA_ALLOC

void ya_alloc_init() {
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

#define ya_block_ptr(ptr) ((block_t*)ptr - 1);
#define ya_block_addr(ptr) ((block_t*)ptr + 1);

#define MAGIC_FREE 999999999
#define MAGIC_USED 888888888
#define MAGIC_END 777777777
#define BLOCK_FREE 123456789
#define BLOCK_USED 987654321

void* ya_sbrk(size_t size) {
  mem_block_t* current = mmt.blocks;
  void* addr = NULL;
  int found = 0;
  while (current) {
    if (current->type == MEM_FREE) {
      if (size <= (current->size - 4096)) {
        addr = current->addr;
        current->addr += size;
        current->size -= size;
        if ((current->size - 4096) <= 0) {
          current->type = MEM_USED;
        }
        found = 1;
        break;
      } else {
        if (current->next == NULL) {
          kassert(size <= current->size);
        }
      }
    }
    current = current->next;
  }
  kassert(found > 0);
  kassert(addr != NULL);
  if (mmt.last_map_addr > 0 &&
      ((u32)addr + PAGE_SIZE * (mmt.extend_phy_count + 10)) >
          mmt.last_map_addr) {
    kprintf("extend kernel phy mem addr:%x last addr:%x extend count:%d\n",
            addr, mmt.last_map_addr, mmt.extend_phy_count);
    // extend 400k*mmt.extend_phy_count phy mem
    for (int i = 0; i < 100 * mmt.extend_phy_count; i++) {
      map_page(mmt.last_map_addr, mmt.last_map_addr,
               PAGE_P | PAGE_USU | PAGE_RWW);
      mmt.last_map_addr += PAGE_SIZE;
    }
    mmt.extend_phy_count++;
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

block_t* ya_find_free_block(size_t size) {
  block_t* block = mmt.g_block_list;
  block_t* find_block = NULL;
  while (block) {
    if (!(block->magic == MAGIC_USED || block->magic == MAGIC_FREE)) {
      log_error("errro find free block addr %x,magic error is %d\n", block,
                block->magic);
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
  if (mmt.g_block_list == NULL) {
    block = ya_new_block(size);
  } else {
    block = ya_find_free_block(size);
  }
  block->free = BLOCK_USED;
  block->magic = MAGIC_USED;
  void* addr = ya_block_addr(block);
  kassert(addr != NULL);
  int* end = addr + block->size;
  kassert((*end) == MAGIC_END);

  block->no = mmt.alloc_count++;
  mmt.alloc_size += size;

#ifdef DEBUG
  kprintf(
      "alloc %x size=%d count=%d total=%dk  baddr=%x bsize=%d bcount=%d last "
      "map=%x\n",
      addr, size, mmt.alloc_count, mmt.alloc_size / 1024, block, block->size,
      block->count, mmt.last_map_addr);
  ya_verify();
  // kprintf("ya_alloc(%d);//no %d addr %x \n", size, block->no, addr);
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
      kassert((current->free == BLOCK_FREE || current->free == BLOCK_USED));
    }
    void* addr = ya_block_addr(current);
    kassert(addr != NULL);
    current = current->next;
    total++;
  }
  kprintf("-------------------------------\n");
  kprintf("verify total %d free %dk used %dk\n", total, free / 1024,
          used / 1024, mmt);
  mem_block_t* block = mmt.blocks;
  while (block) {
    kprintf("=>block:%x type:%d size:%d start: %x end:%x\n", block, block->type,
            block->size, block->addr, block->addr + block->size);
    block = block->next;
  }
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
  // kassert(block->count == 1);
  kassert(block->free == BLOCK_USED);
  kassert(block->magic == MAGIC_USED);

  int* end = ptr + block->size;
  kassert((*end) == MAGIC_END);

  // block->magic = MAGIC_FREE;
  // block->free = BLOCK_FREE;
  // block->count = 0;
  // kmemset(ptr, 0, block->size);
  // block->count = 0;
  // block_t* next = block->next;
  // ptr = NULL;

  // if (next != NULL) {
  //   if (next->free == BLOCK_FREE) {
  //     block->size += next->size + sizeof(block_t);
  //     block->next = next->next;
  //     int size = next->size;
  //     if (next->next) {
  //       next->next->prev = block;
  //     }
  //     // memset(next,0,size);
  //   }
  // }
  // block_t* prev = block->prev;
  // if (prev != NULL) {
  //   if (prev->free == BLOCK_FREE) {
  //     prev->size += block->size;
  //     prev->next = block->next;
  //     if (block->next != NULL) {
  //       block->next->prev = prev;
  //     }
  //     int size = block->size;
  //     // memset(block,0,size);
  //   }
  // }
}

int is_line_intersect(int a1, int a2, int b1, int b2) {
  return !(b2 < a1 || b1 > a2);
}

void mm_add_block(u32 addr, u32 len) {
  mem_block_t* block = addr;
  block->addr = (u32)block + sizeof(mem_block_t);
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
  kprintf("=>block:%x type:%d size:%d start: %x end:%x\n", block, block->type,
          block->size, block->addr, block->addr + block->size);
}

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
  mmt.init = ya_alloc_init;
  mmt.alloc = ya_alloc;
  mmt.free = ya_free;
  mmt.blocks = NULL;
  mmt.blocks_tail = NULL;
  mmt.g_block_list = NULL;
  mmt.g_block_list_last = NULL;
  mmt.alloc_count = 0;
  mmt.alloc_size = 0;
  mmt.last_map_addr = 0;
  mmt.extend_phy_count = 0;

  count = 0;

  kprintf("phy dump\n");
  mm_dump_phy();
  kprintf("alloc init\n");
  // mm init
  mmt.init();
  kprintf("mm init default\n");
  mm_init_default();
}

size_t ya_real_size(void* ptr) {
  block_t* block = ya_block_ptr(ptr);
  return block->size;
}

size_t mm_get_size(void* addr) { return ya_real_size(addr); }

void* mm_alloc(size_t size) { return mmt.alloc(size); }

void mm_free(void* p) { return mmt.free(p); }

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

void mm_add_block(u32 addr, u32 len) {
  mem_block_t* block = addr;
  block->addr = (u32)block + sizeof(mem_block_t);
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
  kprintf("=>block:%x type:%d size:%d start: %x end:%x\n", block, block->type,
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
  u32 pre_alloc_size = size + sizeof(mem_block_t);
  pre_alloc_size = (pre_alloc_size + 8) & ~0x3;
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
      new_block->addr = (u32)new_block + sizeof(mem_block_t);
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
  for (;;)
    ;

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
  if (addr == NULL) return;
  mem_block_t* block = (mem_block_t*)((u32)addr - sizeof(mem_block_t));
  return block->size;
}

void mm_free(void* addr) {
  debug("free %x\n", addr);
  if (addr == NULL) return;
  mem_block_t* block = (mem_block_t*)((u32)addr);
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

void map_mem_block(u32 size, u32 flags) {
  mem_block_t* p = mmt.blocks;
  for (; p != NULL; p = p->next) {
    u32 address = p->origin_addr;
    map_range(address, address, size, flags);
    kprintf("map mem block addr range %x - %x\n", p->origin_addr,
            p->origin_addr + size);
    mmt.last_map_addr = address + size;
  }
}

void map_range(u32 vaddr, u32 paddr, u32 size, u32 flag) {
  int pages = size / PAGE_SIZE + (size % PAGE_SIZE > 0 ? 1 : 0);
  for (int j = 0; j < pages; j++) {
    map_page(vaddr, paddr, flag);
    vaddr += PAGE_SIZE;
    paddr += PAGE_SIZE;
  }
}

void map_kernel(u32 flags) {
  unsigned int address = 0;
  // map kernel
  kprintf("map kernel start\n");
  for (int i = 0; i < boot_info->segments_number; i++) {
    u32 size = boot_info->segments[i].size;
    address = boot_info->segments[i].start;
    map_range(address, address, size, flags);

    kprintf("map kernel %d range %x  - %x\n", i, boot_info->segments[i].start,
            address + size);
  }
  kprintf("map kernel end %d\n", boot_info->segments_number);
}
