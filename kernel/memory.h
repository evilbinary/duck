/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef MEMORY_H
#define MEMORY_H

#include "arch/arch.h"
#include "kernel/config.h"

#ifdef X86
#define EXEC_ADDR 0x40000000  // 1gb exec space to stack
#define STACK_ADDR 0x80000000
#define HEAP_ADDR 0x82000000
#define KERNEL_OFFSET 0x10000000

#elif defined(ARM64) || defined(__aarch64__)
// UL suffix is required: without it, constants like 0x90000000 (STACK_ADDR +
// KERNEL_OFFSET) are signed int on 32-bit arithmetic, and when cast to a
// 64-bit pointer they get sign-extended to 0xffffffff90000000.
#define KERNEL_OFFSET 0x20000000UL
#define EXEC_ADDR     0x60000000UL
#define STACK_ADDR    0x70000000UL
#define HEAP_ADDR     0x70100000UL

#elif defined(ARM)
#define KERNEL_OFFSET 0x20000000 //
#define EXEC_ADDR  0x60000000   // 0x60000000-0x66400000 0xc0000000
#define STACK_ADDR 0x70000000   // 0x70000000-0x70100000 0xd0000000
#define HEAP_ADDR  0x70100000   // 0x70100000-0x76500000 0xd0000000

#elif defined(RISCV)
#define KERNEL_OFFSET 0x20000000 //
#define EXEC_ADDR  0x60000000   // 0x60000000-0x66400000 0xc0000000
#define STACK_ADDR 0x70000000   // 0x70000000-0x70100000 0xd0000000
#define HEAP_ADDR  0x70100000   // 0x70100000-0x76500000 0xd0000000
#else
#define KERNEL_OFFSET 0x10000000
#define EXEC_ADDR  0x60000000  // 0x60000000-0x66400000
#define STACK_ADDR 0x70000000 // 0x70000000-0x70100000
#define HEAP_ADDR  0x70100000  // 0x70100000-0x76500000
#endif

#define MEMORY_FREE 0
#define MEMORY_USE 1
#define MEMORY_SHARE 2
#define MEMORY_HEAP 3
#define MEMORY_EXEC 4
#define MEMORY_DATA 5
#define MEMORY_STACK 6
#define MEMORY_MMAP 7
#define MEMORY_DEV 8
/* 帧缓冲：物理页已存在（通常由内核页表映射），用户态按需镜像时要用
 * PAGE_FB（可缓存写回，配合驱动每帧 clean），而不是 MEMORY_DEV 的 PAGE_DEV。
 * 之前是靠在 page_fault_handle 里用 paddr/fault_addr >= 0xF0000000 判断，
 * 平台相关且会误伤普通高位地址。 */
#define MEMORY_FB 9

#define MEMORY_STACK_SIZE 1024 * 1024   // 1m
/* 【2026-10 扩窗实验·根因已定位】最初 1GB 窗口 shell 即卡死，曾误归因于
 * int sys_brk/u32 try/page_destroy 上界（已顺手修对），真根因有二：
 * 1) 内核线程 VMA 区（HEAP_ADDR+KERNEL_OFFSET=0x90100000）与扩大的用户
 *    窗口重叠 512MB ⇒ 用户高地址映射踩内核结构。已修：vma.c 内核线程
 *    VMA 搬到 USER_VA_END 之上（koffset=USER_VA_END）。
 * 2) 非用户线程 kmalloc(DEFAULT_TYPE) 走 vm_alloc()，从内核线程堆 VMA 里
 *    bump 出幽灵地址（0x90100040/0x130100000，从未映射，靠 QEMU/硬件
 *    静默吞写侥幸可用）⇒ /dev/stdout 的 vnode->device 变垃圾 ⇒ write 恒
 *    返 0 ⇒ musl stdio 无限重试 writev ⇒ /bin/config 自旋吃满 CPU ⇒
 *    shell 假死。已修：memory.c kmalloc 非用户线程统一 phy_alloc。
 * 修复后 QEMU raspi3 实测：config 正常跑完 init.conf，shell 交互（ps 等）
 * 正常，空闲 CPU 回到 cpu_wait。 */
#define MEMORY_HEAP_SIZE 1024 * 1024 * 1024  // 1g
#define MEMORY_EXEC_SIZE 1024 * 1024 * 100  // 100m

/* 【用户 VA 上界（排他）】page_destroy 靠它判断"这个 PTE 属于用户、可归还"。
 * 必须与 MEMORY_HEAP_SIZE 联动（扩窗时一起调），pgd 索引按它【向上取整】；
 * 内核线程 VMA 也以它为界（vma.c 的 koffset）。FB 别名 0xfb000000 在
 * pgd[3]，扫描上界之外不会被误归还。 */
#define USER_VA_END 0xC0000000UL

#define KERNEL_POOL_NUM 10
#define USER_POOL_NUM 10

#define MEMORY_TYPE_USE 1   // 使用
#define MEMORY_TYPE_FREE 2  // 释放

#define MEMORY_ALIGMENT 16

#define DEFAULT_TYPE 1 << 0
#define KERNEL_TYPE 1 << 1
#define DEVICE_TYPE 1 << 2

typedef struct memory {
  ulong total;
  ulong free;
  ulong kernel_used;
  ulong user_used;
} memory_t;

typedef struct vmemory_area {
  vaddr_t vaddr;
  vaddr_t vend;
  vaddr_t size;
  u8 flags;
  u32 attr; /* 页表映射属性(PAGE_DEV/PAGE_FB/PAGE_USER/PAGE_SHARED...)。
              * arm64 的 PAGE_DEV 含 bit8 的 shareability，u8 会截掉。 */
  vaddr_t alloc_addr;
  vaddr_t alloc_size;
  /* 【文件映射·按需分页】file_node != NULL 表示该 VMA 是文件映射：
   * mmap 时只登记【不分配物理页、不读文件】，缺页时 page.c 逐页
   * valloc + vread 补内容（off = file_off + (fault - vaddr)）。
   * 大文件（如 810MB 模型）只有真正被访问的页才占物理内存。
   * 注意：fatfs close 会关 FIL，若进程 mmap 后 close(fd)，后续缺页
   * vread 会失败——映射期间请保持 fd 打开（yllm 当前如此）。 */
  void* file_node;
  u64 file_off; /* 映射起点在文件中的字节偏移（须页对齐） */
  struct vmemory_area* next;
  struct vmemory_area* child;
} vmemory_area_t;

typedef struct vmemory{
  void* upage;
  void* kpage;
  vmemory_area_t* vma;
  u32 tid;
  u32 ref; /* shared by CLONE_VM threads; free only when last ref drops */
}vmemory_t;


#define ALIGN(x, a) (x + (a - 1)) & ~(a - 1)

#ifdef MALLOC_TRACE
#define kmalloc(size, flag) \
  kmalloc_trace(size, flag, __FILE__, __LINE__, __FUNCTION__)
#define kmalloc_alignment(size, alignment, flag)                     \
  kmalloc_alignment_trace(size, alignment, flag, __FILE__, __LINE__, \
                          __FUNCTION__)
#define kfree(ptr) kfree_trace(ptr, __FILE__, __LINE__, __FUNCTION__)
#define kfree_alignment(ptr) \
  kfree_alignment_trace(ptr, __FILE__, __LINE__, __FUNCTION__)

#else
void* kmalloc(size_t size, u32 flag);
void* kmalloc_alignment(size_t size, int alignment, u32 flag);

void kfree(void* ptr);
void kfree_alignment(void* ptr);
#endif

void* valloc(void* addr, size_t size);
/* 不清零分配：调用方保证全量覆写（文件映射块填充用，尾部自清） */
void* valloc_nozero(void* addr, size_t size);
void vfree(void* addr, size_t size);

void kpool_init();
void* kpool_poll();
int kpool_put(void* e);

void use_kernel_page();
void use_user_page();

void* page_v2p(u64* page_dir_ptr_tab, void* vaddr);
void* kpage_v2p(void* addr, int size);

/* vmemory 映射（定义在 vma.c）：原型必须对调用方可见（thread.c / sysfn.c /
 * loader），否则走隐式声明会让参数寄存器错位、属性丢失。 */
void vmemory_map(void* page_dir, vaddr_t virt_addr, vaddr_t phy_addr,
                 vaddr_t size);
void vmemory_map_type(void* page_dir, vaddr_t virt_addr, vaddr_t phy_addr,
                      vaddr_t size, u32 type);

void memory_init();

/* 【物理页归还统一入口】持 memory_lock 过引用计数闸后归还页池（vfree 与
 * swap 回收共用，swap 模块不直接碰 memory_lock）。 */
void memory_free_phys_page(void* phy);

/* ---- 页交换（swap）hook ----
 * 内核只认这组 hook 指针，不 include 模块头：依赖方向是 module → kernel，
 * 由 swap 模块（modules/swap）init 时 page_swap_ops_register() 注册实现。
 * 未注册 = 无 swap，全部默认实现直通（lookup 返 0、reclaim 返 0），
 * 内核路径零开销、零链接依赖。语义详见 modules/swap/swap.c 对应实现。 */
typedef struct page_swap_ops {
  u32 (*lookup)(void* vm, vaddr_t va);     /* (vm,va) 有换出记录？返回槽号，0=无 */
  void (*forget)(void* vm, vaddr_t va);    /* 删除一条记录（换入成功后） */
  void (*drop_vm)(void* vm);               /* 地址空间销毁/VA 复用前清全部记录 */
  int (*reclaim)(u32 npages, u32 skip_pa); /* 页池不足时换出，返回换出页数 */
  int (*read_page)(u32 slot, void* page);  /* 换入：读一个槽的整页，0=成功 */
  int (*swapback_vm)(void* vm);            /* fork 前：把 vm 全部换出页换回内存 */
} page_swap_ops_t;
void page_swap_ops_register(const page_swap_ops_t* ops);

u32 page_swap_lookup(void* vm, vaddr_t va);
void page_swap_forget(void* vm, vaddr_t va);
void page_swap_drop_vm(void* vm);
int page_swap_reclaim(u32 npages, u32 skip_pa);
int page_swap_read_page(u32 slot, void* page);
int page_swap_swapback_vm(void* vm);

/* valloc 失败后一次回收的目标页数：一次多换几页摊薄 IO 次数 */
#define PAGE_SWAP_RECLAIM_BATCH 8

vmemory_area_t* vmemory_area_create(void* addr, vaddr_t size, u8 flags);

/* 同上，但显式指定该 VMA 的页表映射属性（0 = 按 flags 推导）。 */
vmemory_area_t* vmemory_area_create_attr(void* addr, vaddr_t size, u8 flags,
                                         u32 attr);

/* 【设备/帧缓冲一步登记】物理页已存在的区域（MMIO 寄存器窗口、显存）：
 * 一次调用同时 1) 映射进内核页表（内核驱动可直接访问）；2) 记入默认 VMA
 * 模板，之后每个新进程的 VMA 链都会带上它（用户态首次访问时缺页按 VMA 的
 * attr 镜像）。paddr==0 表示恒等映射(vaddr)。
 * 这样就不用手写 create + add + page_map 三处。 */
void vmemory_map_phys(vaddr_t vaddr, vaddr_t paddr, vaddr_t size, u8 flags,
                      u32 attr);
vmemory_area_t* vmemory_area_destroy(vmemory_area_t* area);

vmemory_area_t* vmemory_area_alloc(vmemory_area_t* areas, void* addr, vaddr_t size);
vmemory_area_t* vmemory_area_find(vmemory_area_t* areas, void* addr,
                                  vaddr_t size);
vmemory_area_t* vmemory_area_find_flag(vmemory_area_t* areas, u32 flags);
void vmemory_area_add(vmemory_area_t* areas, vmemory_area_t* area);
void vmemory_area_free(vmemory_area_t* area);
/* 只释放 vma 链表节点（含 child 递归），不 vfree 页 —— 页/页表由
 * vmemory_destroy → page_destroy 统一归还，见 vma.c 实现处注释。 */
void vmemory_area_destroy_list(vmemory_area_t* area);
void vmemory_destroy(vmemory_t* vm);
/* 把 file_node 文件映射区回写文件（munmap/进程退出时调用） */
void vmemory_flush_shared_files(vmemory_t* vm);
/* 【exec 用】回收当前进程用户堆区里已映射的页（含 brk 堆/映像/mmap 区）
 * 并复位 brk 前沿、释放 mmap 节点；页表保留复用。栈区不动（见 vma.c 注释）。
 * 供 loader 在"原地 exec 替换映像"前调用，避免旧进程占用被带到新映像。 */
void vmemory_release_user_space(vmemory_t* vm);
void memory_static(u32 size, int type);

int memory_stack_ensure(vmemory_t* vm, u32 sp, u32 need);

#endif
