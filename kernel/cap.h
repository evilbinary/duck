/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef KERNEL_CAP_H
#define KERNEL_CAP_H

#include "kernel/kernel.h"

/* ============ capability 系统 v2 ============
 * 三层：用户态句柄 → CSpace（TCB 内的槽数组）→ 内核对象。
 *
 * 【为什么归内核】cap 是所有资源访问的统一入口（sys_thread_map、sys_mmap、
 * irq、sched 都要查），比 foot 服务框架更底层。放模块里会让 posix 层反向
 * 依赖服务模块，只能靠弱符号（"没链接就静默放行"）绕过。
 *
 * 【为什么挂 TCB】句柄隔离粒度最细，撤销边界清晰；同进程线程共享靠显式
 * 引用计数（cspace->ref），ClOSE 语义与 Linux fd 表一致。详见
 * docs/develop/微内核.md 5.2 / 5.10。 */

/* ---- 对象类型（类型注册表索引） ---- */
typedef u16 obj_type_t;
enum {
  OBJ_NONE = 0,
  OBJ_UNTYPED,  /* 物理内存块，可 retype */
  OBJ_FRAME,    /* 物理页，可 MAP */
  OBJ_ARENA,    /* 物理连续内存（DMA / 渲染目标） */
  OBJ_DEVWIN,   /* 设备寄存器窗口 */
  OBJ_IRQ,      /* 中断事件 */
  OBJ_ENDPOINT, /* IPC 端点 */
  OBJ_TCB,      /* 线程控制块引用 */
  OBJ_TYPE_MAX
};

/* ---- 通用权限（低 16 位，所有类型语义一致） ---- */
enum {
  RIGHT_LOAD = 1 << 0,    /* 读对象状态/数据 */
  RIGHT_STORE = 1 << 1,   /* 改对象状态/数据 */
  RIGHT_INVOKE = 1 << 2,  /* 调用对象方法 */
  RIGHT_GRANT = 1 << 3,   /* 复制到其它 CSpace */
  RIGHT_MINT = 1 << 4,    /* 本 CSpace 内派生受限能力 */
  RIGHT_REVOKE = 1 << 5,  /* 撤销自己派生出的能力 */
  RIGHT_DESTROY = 1 << 6, /* 销毁对象 */
  RIGHT_CREATE = 1 << 7,  /* 基于该对象创建子对象 */
};

/* ---- 类型位（高 16 位，按类型解释；不同类型可复用同一位） ---- */
enum {
  FRAME_MAP = 1 << 16,
  FRAME_UNMAP = 1 << 17,

  EP_SEND = 1 << 16,
  EP_RECV = 1 << 17,
  EP_NOTIFY = 1 << 18,

  IRQ_ACK = 1 << 16,
  IRQ_BIND = 1 << 17,

  DEVWIN_MAP = 1 << 16,
  DEVWIN_MAILBOX = 1 << 17,

  TCB_READ_REGS = 1 << 16,
  TCB_WRITE_REGS = 1 << 17,
  TCB_SUSPEND = 1 << 18,
  TCB_RESUME = 1 << 19,
};

/* 对象槽上限（全局对象表） */
#define OBJ_MAX 256

typedef struct kobject {
  obj_type_t ty;
  u16 pad;
  u32 id;     /* 全局唯一 id = 槽号 + 1 */
  u64 epoch;  /* 生命周期版本号：销毁时 +1，能力 epoch 不符即 stale */
  void* state;/* 具体对象数据，由类型 ops 解释 */
  u32 refs;   /* 存活能力数，归零才 destroy */
} kobject_t;

/* ---- 能力槽位 ---- */
typedef struct cap {
  kobject_t* obj; /* 对象引用 */
  obj_type_t ty;  /* 冗余，快速类型校验 */
  u16 pad;
  u32 rights;   /* 通用位 | 类型位 */
  u64 epoch;    /* 从对象头复制 */
  u32 parent;   /* 派生链：父槽号，0 = 根 */
  u32 slot;     /* 自身槽号（删除时清槽，防 double free） */
  /* 映射台账：最近一次 map 的 VA/长度，撤销时据此 unmap。
   * 同一能力映射多次时只记最后一次，其余由进程退出的 page_destroy 兜底
   * （设备映射的 VA < EXEC_ADDR，那里只清 PTE 不还页）。 */
  vaddr_t map_va;
  vaddr_t map_len;
  vaddr_t map_off; /* 最近一次 map 在对象内的偏移（FRAME 归还引用要用） */
} cap_t;

#define CSPACE_MIN_SLOTS 64
#define CSPACE_MAX_SLOTS 4096
#define CAP_SLOT_BITS 12
#define CAP_SLOT_MASK ((1u << CAP_SLOT_BITS) - 1)
#define CAP_GEN_MASK 0xFFFFFu /* 20 位 */

typedef struct cspace {
  cap_t* slots; /* 槽数组（动态扩容 64 → 4096） */
  u32* gens;    /* 每槽代次，防 ABA */
  u32 nslot;    /* 已分配容量 */
  u32 ref;      /* 共享该表的 TCB 数（CLONE_VM / vfork） */
} cspace_t;

/* ============ 对象层（cap_obj.c） ============ */
typedef struct obj_ops {
  obj_type_t ty;
  const char* name;
  /* 类型相关方法。arg 由调用方按类型解释（DEVWIN: cap_devwin_arg_t）。 */
  int (*invoke)(kobject_t* obj, cap_t* c, void* arg);
  void (*destroy)(kobject_t* obj); /* 最后一个能力消失 */
  int (*map)(kobject_t* obj, void* pgdir, vaddr_t va, vaddr_t off, vaddr_t len);
  void (*unmap)(kobject_t* obj, void* pgdir, vaddr_t va, vaddr_t off,
                vaddr_t len);
} obj_ops_t;

void obj_init(void);
int obj_register(const obj_ops_t* ops);
const obj_ops_t* obj_ops_get(obj_type_t ty);
/* 新建对象：refs 置 1（属于第一个能力），返回 NULL 表示对象表满 */
kobject_t* obj_create(obj_type_t ty, void* state);
kobject_t* obj_get(u32 id);
void obj_ref(kobject_t* o);
/* 释放一个能力引用；refs 归零 → ops->destroy + epoch++ */
void obj_put(kobject_t* o);
/* 按类型遍历（返回第一个匹配，调试/按名字查窗口用） */
kobject_t* obj_find(obj_type_t ty, int (*match)(kobject_t*, const char*),
                    const char* arg);

/* ============ CSpace 层（cap.c） ============ */
/* boot 初始化：对象表清零 + 注册对象类型 + 建立静态设备窗口对象 */
void cap_init(void);

cspace_t* cspace_create(void);
void cspace_destroy(cspace_t* cs, void* pgdir);
/* share=1：同一个指针 ref++（CLONE_VM/vfork）；share=0：复制整表（fork） */
cspace_t* cspace_share_or_dup(cspace_t* src, int share);
void cspace_put(cspace_t* cs, void* pgdir);
/* 解析句柄；非法返回 NULL */
cap_t* cspace_get(cspace_t* cs, u32 handle);
/* 在 cs 里安装一个能力（obj 已 ref 过或新建），返回句柄 */
u32 cap_install(cspace_t* cs, kobject_t* obj, u32 rights, u32 parent_slot);

/* 原语 */
int cap_copy(cspace_t* src, u32 src_h, cspace_t* dst, u32* out_h);
int cap_move(cspace_t* src, u32 src_h, cspace_t* dst, u32* out_h);
u32 cap_mint(cspace_t* cs, u32 h, u32 rights);
void cap_delete(cspace_t* cs, u32 h, void* pgdir);
void cap_revoke(cspace_t* cs, u32 h, void* pgdir);
int cap_invoke(cspace_t* cs, u32 h, void* arg);
int cap_info(cspace_t* cs, u32 h, u32* ty, u32* rights, u32* obj_id,
             u64* epoch);

/* 当前线程的 CSpace（内核线程为 NULL） */
cspace_t* cap_current(void);

/* 判断 [pa, pa+size) 是否落在持有者某个 DEVWIN 窗口 / FRAME 能力覆盖的
 * 范围之内（sys_thread_map 收权用），1 = 允许 */
int cap_check_phys_range(cspace_t* cs, vaddr_t pa, vaddr_t size, u32 need_rights);
/* 同上，但返回覆盖该范围的能力种类：0 = 无，1 = DEVWIN，2 = FRAME。
 * 调用方据此决定是否要为自己的映射再持有一份页引用（帧需要）。 */
#define CAP_RANGE_NONE 0
#define CAP_RANGE_DEVWIN 1
#define CAP_RANGE_FRAME 2
int cap_range_owner(cspace_t* cs, vaddr_t pa, vaddr_t size, u32 need_rights);

/* ---- 类型无关的用法（按 c->ty 分派到 obj_ops） ---- */
/* 用能力把对象映射进 pgdir：DEVWIN 需 DEVWIN_MAP，FRAME 需 FRAME_MAP。
 * 成功后把 VA/长度记进能力台账，撤销时据此 unmap。 */
int cap_map(cspace_t* cs, u32 h, void* pgdir, vaddr_t vaddr, vaddr_t offset,
            vaddr_t len);
int cap_unmap(cspace_t* cs, u32 h, void* pgdir);
/* 取对象资源信息（DEVWIN: 窗口基址/长度；FRAME: 物理页基址/长度） */
int cap_res_info(cspace_t* cs, u32 h, u32* pa, u32* size, u32* rights);

/* ============ OBJ_DEVWIN（cap_devwin.c） ============ */
typedef struct foot_dev_win {
  const char* name;
  vaddr_t base;
  vaddr_t size;
  u32 irq;
} foot_dev_win_t;

/* DEVWIN 的默认授予权限：读写寄存器 + 可映射 + 可派生/撤销/转授 */
#define CAP_DEVWIN_RIGHTS_DEFAULT                                              \
  (RIGHT_LOAD | RIGHT_STORE | RIGHT_INVOKE | RIGHT_GRANT | RIGHT_MINT |        \
   RIGHT_REVOKE | DEVWIN_MAP | DEVWIN_MAILBOX)

/* boot 期把静态窗口表建成 OBJ_DEVWIN 对象（内核自用，不面向用户） */
void cap_devwin_init(void);
const foot_dev_win_t* cap_devwin_find(const char* name);
/* 把窗口对象的一个能力装进 cs（v2 过渡：按名字；最终由 boot 授权表/copy） */
u32 cap_devwin_grant(cspace_t* cs, const char* name);
/* 映射窗口内的页到 pgdir（DEVWIN_MAP） */
int cap_devwin_map(cspace_t* cs, u32 h, void* pgdir, vaddr_t vaddr,
                   vaddr_t offset, vaddr_t len);
/* 解除该能力记录过的映射 */
int cap_devwin_unmap(cspace_t* cs, u32 h, void* pgdir);
/* 查窗口物理基址 / 长度 / 当前权限（指针可空） */
int cap_devwin_info(cspace_t* cs, u32 h, u32* pa, u32* size, u32* rights);

/* ============ OBJ_FRAME（cap_frame.c） ============
 * 物理页（或页组）对象。两种来源：
 *   1) pa == 0：由页池分配，owned=1，最后一个能力消失时归还页池；
 *   2) pa != 0：包装已存在的物理页（DMA 缓冲 / 共享页），owned=0，
 *      对象消失只是"没人持有"，页面本身不动。
 * 用户态拿到的句柄可用于 mmap 式映射（FRAME_MAP）或被 sys_thread_map 认可。 */
typedef struct foot_frame {
  vaddr_t pa;   /* 物理基址（页对齐） */
  vaddr_t size; /* 字节数（页对齐） */
  u32 owned;    /* 1 = 页由本对象分配，销毁时归还 */
  u32 owner;    /* 创建者线程 id（审计用） */
} foot_frame_t;

/* FRAME 的默认授予权限：读 + 可映射 + 可派生/撤销/转授 */
#define CAP_FRAME_RIGHTS_DEFAULT                                               \
  (RIGHT_LOAD | RIGHT_INVOKE | RIGHT_GRANT | RIGHT_MINT | RIGHT_REVOKE |       \
   FRAME_MAP | FRAME_UNMAP)

/* 注册 OBJ_FRAME 类型（boot 期由 cap_init 调用） */
void cap_frame_init(void);
/* 创建帧对象并把能力装进 cs，返回句柄（0 = 失败）。
 *   size 按页向上取整；pa == 0 表示从页池新分配，否则包装该物理地址。
 *   rights == 0 时取 CAP_FRAME_RIGHTS_DEFAULT。 */
u32 cap_frame_alloc(cspace_t* cs, vaddr_t size, vaddr_t pa, u32 rights);
/* 映射帧内 [offset, offset+len) 到 vaddr（FRAME_MAP） */
int cap_frame_map(cspace_t* cs, u32 h, void* pgdir, vaddr_t vaddr,
                  vaddr_t offset, vaddr_t len);
/* 解除该能力记录过的映射 */
int cap_frame_unmap(cspace_t* cs, u32 h, void* pgdir);
/* 查帧物理基址 / 长度 / 当前权限（指针可空） */
int cap_frame_info(cspace_t* cs, u32 h, u32* pa, u32* size, u32* rights);

/* ============ OBJ_ENDPOINT（cap_endpoint.c） ============
 * 内核消息队列 + 随消息转授能力（IPC grant）。
 *   一个 endpoint 对象就是一端"信箱"：任何持有该对象 EP_SEND 能力的线程
 *   可以投递消息，持有 EP_RECV 的线程可以取走。
 *   fork 之后父子自动共享同一个 endpoint（CSpace 整表复制，句柄值一致），
 *   这正是"父进程把某个能力交给子进程"的最小通路：
 *     父 send {buf, grant=dwc2 句柄} → 子 recv → 得到一个可用的新句柄。
 *
 * 【为什么由内核完成转授】能力表挂在 TCB 上，用户态无法直接写对方 CSpace；
 * 发送时内核先给对象补一份引用，接收时再装进接收者 CSpace，引用随之转移。
 * 队列满/空返回 -1（非阻塞，调用方轮询），阻塞版本要等调度器配合。 */
#define CAP_EP_MSGS 8
#define CAP_EP_DATA_MAX 64

/* endpoint 的默认权限：收发消息 + 可转授/派生/撤销 */
#define CAP_ENDPOINT_RIGHTS_DEFAULT                                            \
  (RIGHT_LOAD | RIGHT_STORE | RIGHT_INVOKE | RIGHT_GRANT | RIGHT_MINT |        \
   RIGHT_REVOKE | EP_SEND | EP_RECV)

/* 发送/接收的参数（与 eggs/libfoot/client.h 的 foot_ep_ctl_t 逐字段一致） */
typedef struct cap_ep_ctl {
  u32 handle;    /* endpoint 句柄 */
  u32 grant;     /* SEND：随消息一起转授的句柄（0 = 不带）；
                  * RECV：输出收到的句柄（0 = 本条没带） */
  u32 len;       /* SEND：数据长度；RECV：缓冲区容量 */
  u32 out_len;   /* RECV：实际数据长度 */
  u32 rights;    /* SEND：给接收方的权限（0 = 沿用原能力权限） */
  u32 flags;     /* CAP_EP_NONBLOCK 见下 */
  char* buf;     /* 数据缓冲区（用户指针） */
} cap_ep_ctl_t;

/* flags 位：不带该位时收发是阻塞的（挂起直到有消息/有空位） */
#define CAP_EP_NONBLOCK 1u

/* 注册 OBJ_ENDPOINT 类型（boot 期由 cap_init 调用） */
void cap_endpoint_init(void);
/* 新建 endpoint 并把能力装进 cs（EP_SEND | EP_RECV | GRANT/MINT/REVOKE） */
u32 cap_endpoint_create(cspace_t* cs);
/* 投递一条消息；grant_h 非 0 时把该能力随消息转授（需要 RIGHT_GRANT） */
int cap_endpoint_send(cspace_t* cs, u32 h, const void* data, u32 len,
                      u32 grant_h, u32 grant_rights);
/* 取一条消息；copy 到 data（最多 cap 字节），带上转授的能力句柄 */
int cap_endpoint_recv(cspace_t* cs, u32 h, void* data, u32 cap, u32* out_len,
                      u32* out_grant);
/* 同上，但可指定完成后是否唤醒对端等待者。阻塞实现（service.c）在
 * poll_wait_suspend 的重查回调里必须传 wake=0，否则 notify → recheck →
 * send → notify 会自递归。 */
int cap_endpoint_send_ex(cspace_t* cs, u32 h, const void* data, u32 len,
                         u32 grant_h, u32 grant_rights, int wake);
int cap_endpoint_recv_ex(cspace_t* cs, u32 h, void* data, u32 cap, u32* out_len,
                         u32* out_grant, int wake);

#endif
