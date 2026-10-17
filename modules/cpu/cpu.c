/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
/* CPU 温度模块：抽象层。
 *
 * 只做三件事：把 /dev/cpu 节点挂上、定义 read 语义、把前端请求转给后端
 * cpu_temp_read()。硬件怎么读温度由各平台文件实现（见 cpu.h 的分层说明）：
 *   raspi5.c   firmware mailbox（独立于风扇/RP1，链路没起来也能读）
 *   general.c  没有温度源的平台，返回失败
 *
 * 【接口】/dev/cpu：read 4 字节 = 毫摄氏度（读失败返回 0 字节）。
 * 用户态温控策略见 app/cmd/fand.c。
 ********************************************************************/
#include "cpu.h"
#include "kernel/memory.h"
#include "dev/devfs.h"

/* /dev/cpu：read 4 字节 = 毫摄氏度 */
static size_t cpu_read(device_t* dev, void* buf, size_t len) {
  (void)dev;
  int mc = 0;
  if (len < sizeof(int)) return 0;
  if (cpu_temp_read(&mc) != 0) return 0; /* 后端不可用 -> 0 字节 */
  *(int*)buf = mc;
  return sizeof(int);
}

/* 【函数名不能用 cpu_init】duck/arch 下已有同名的 arch 层初始化函数，
 * 链接期会 conflicting types；模块实现统一加 _temp 前缀 */
int cpu_temp_init(void) {
  /* 启动即读一次，验证温度链路通不通（室温应 30-50°C）。具体失败原因
   * （mailbox 响应码等）由后端自己打印，这里只报结论。 */
  int mc = 0;
  if (cpu_temp_read(&mc) == 0) {
    kprintf("cpu: temperature %d.%d c\n", mc / 1000, (mc % 1000) / 100);
  } else {
    kprintf("cpu: no temperature source, /dev/cpu reads 0 bytes\n");
  }

  device_t* cpu_dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  cpu_dev->name = "cpu";
  cpu_dev->read = cpu_read;
  cpu_dev->id = DEVICE_CPU;
  cpu_dev->type = DEVICE_TYPE_CHAR;
  device_add(cpu_dev);

  vnode_t* vnode = vfs_create_node("cpu", V_FILE);
  vfs_mount(NULL, "/dev", vnode);
  vnode->device = device_find(DEVICE_CPU);
  vnode->op = &device_operator;

  return 0;
}

void cpu_temp_exit(void) {}

module_t cpu_module = {.name = "cpu",
                       .init = cpu_temp_init,
                       .exit = cpu_temp_exit};
