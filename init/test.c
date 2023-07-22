#include "kernel/kernel.h"
#include "shell.h"

#ifdef XTENSA

void test_gui() {}
#else

void test_gui() {
  // char wheel[] = {'\\', '|', '/', '-'};
  // screen_init();
  // int i = 0, j = 0;
  // for (;;) {
  //   screen_printf(0, 0, "Hello YiYiYa Os\n");
  //   screen_fill_rect(0, 40, 30, 30, 0x00ff00);
  //   screen_draw_line(0, 0, 140, 140, 0xff0000);
  //   screen_flush();
  // }
}

int test_ioctl(int fd, int cmd, ...) {
  void* arg;
  va_list ap;
  va_start(ap, cmd);
  arg = va_arg(ap, void*);
  va_end(ap);
  int ret = syscall3(SYS_IOCTL, fd, cmd, arg);
  return ret;
}

#define RED 0xf800
#define BLUE 0x001f
#define GREEN 0x07e0
#define YELLOW 0xffe0
#define MAGENTA 0xF81F
#define CYAN 0xFFE0

#define IOC_READ_FRAMBUFFER_INFO _IOW('v', 8, int)
typedef struct framebuffer_info {
  u32 width;
  u32 height;
  u32 bpp;
  u32 mode;
  u32* frambuffer;
  u32 framebuffer_count;
  u32 framebuffer_index;
  u32 framebuffer_length;
  u32 inited;
  u32* write;
  u32* flip_buffer;
} my_framebuffer_info_t;

void test_lcd() {
  my_framebuffer_info_t fb;
  u16 buf[] = {12, 12, BLUE};

  int fd = syscall2(SYS_OPEN, "/dev/fb", 0);
  kprintf("screen init fd:%d\n", fd);
  int myfd = fd;
  test_ioctl(fd, IOC_READ_FRAMBUFFER_INFO, &fb);
  kprintf("fd %d buf info %d %d\n", myfd, fb.width, fb.height);

  for (int i = 0; i < 10; i++) {
    int ret = syscall3(SYS_WRITE, 3, buf, sizeof(buf));
    kprintf("ret %d\n", ret);
  }
}

void test_cpu_speed() {
  for (;;) {
    int* p = 0xfb000000;
    for (int i = 0; i < 480; i++) {
      for (int j = 0; j < 272; j++) {
        *p++ = 0xff0000;
      }
    }
    kprintf("flush=>\n");
  }
}

#define IOC_AHCI_MAGIC 'a'
#define IOC_READ_OFFSET _IOW(IOC_AHCI_MAGIC, 3, int)
#define IOC_WRITE_OFFSET _IOW(IOC_AHCI_MAGIC, 4, int)

#define memset kmemset
void test_SYS_DEV_READ_write() {
  char* test = "hello,do serial thread\n";
  char wheel[] = {'\\', '|', '/', '-'};
  char buf[512];
  int count = 0, i = 0;
  memset(buf, 1, 512);
  // syscall3(SYS_WRITE, DEVICE_SERIAL, test, kstrlen(test));
  syscall1(SYS_PRINT, "2");
  if (count % 100 == 0) {
    // syscall1(SYS_PRINT, "\n");
  }
  int fd = syscall2(SYS_OPEN, "/B.TXT", 0);
  test_gui();
  syscall1(SYS_PRINT, "\n");
  syscall3(SYS_READ, fd, buf, 512);
  syscall3(SYS_PRINT_AT, &wheel[i++], 100, 1);
  syscall3(SYS_DEV_READ, DEVICE_SATA, buf, 512);
  syscall3(SYS_DEV_IOCTL, DEVICE_SATA, IOC_WRITE_OFFSET, 0x400);
  memset(buf, 1, 512);
  syscall3(SYS_DEV_WRITE, DEVICE_SATA, buf, 512);
  memset(buf, 0, 512);
  syscall3(SYS_DEV_READ, DEVICE_SATA, buf, 512);
}

#endif

void test_syscall() {
  u8 scan_code;
  u8 shf_p = 0;
  u8 ctl_p = 0;
  u8 alt_p = 0;
  u32 col = 0;
  u32 row = 0;
  int count = 0, i = 0;
  int fd = 0;
  char buf[28];
  syscall1(SYS_PRINT, "1");
  if (count % 100 == 0) {
    syscall1(SYS_PRINT, "\n");
  }
  int ret = syscall3(SYS_READ, fd, &scan_code, 1);
  if (ret >= 1) {
    // kprintf("ret=%d %x", ret,scan_code);
    if (scan_code & 0x80) return;
    syscall3(SYS_PRINT_AT, buf, col, row);
    if (scan_code == 0x1c) {
      row++;
      col = 0;
      syscall1(SYS_PRINT, "#");
    }
    scan_code = 0;
    col++;
  }
}

void run_test_pool_case(u32 pool_size, size_t alloc_size, u32 align) {
  kprintf("Running test case with pool size %d, alloc size %d, align %d\n",
          pool_size, alloc_size, align);

  pool_t* pool = pool_create(pool_size);
  if (pool == NULL) {
    kprintf("Failed to create memory pool\n");
    return;
  }

  void* mem = pool_alloc(pool, alloc_size, align);
  if (mem == NULL) {
    kprintf("Failed to allocate memory\n");
    pool_destroy(pool);
    return;
  }

  kprintf("Memory allocated successfully at address: %p\n", mem);

  size_t available = pool_available(pool);
  kprintf("Available memory in the pool: %d\n", available);

  pool_destroy(pool);

  kprintf("Memory pool destroyed\n");
}

void test_pool() {
  // Test case 1: Allocating memory within pool size
  run_test_pool_case(1024, 128, 4);

  // Test case 2: Allocating memory exceeding pool size
  run_test_pool_case(256, 512, 4);

  // Test case 3: Allocating memory with larger alignment requirement
  run_test_pool_case(512, 64, 16);
}

void run_test_queue_pool_case(u32 size, u32 poll_size, u32 bytes, u32 align) {
  kprintf("Running test case with size %u, bytes %u, align %u\n", size, bytes,
          align);

  // 创建队列池
  queue_pool_t* q;
  if (align > 0) {
    q = queue_pool_create_align(size, bytes, align);
  } else {
    q = queue_pool_create(size, bytes);
  }
  if (q == NULL) {
    kprintf("Failed to create queue pool\n");
    return;
  }

  // 随机生成一些元素
  int* elements[size];
  for (int i = 0; i < size; i++) {
    elements[i] = kmalloc(bytes, KERNEL_TYPE);
    // 这里假设生成的元素是整数，为了简化测试
    *elements[i] = i;
  }

  // 将元素放入队列池
  for (int i = 0; i < size; i++) {
    if (queue_pool_put(q, elements[i]) != 0) {
      kprintf("Success to put element %d into the queue pool\n", i);
    }
  }

  // 验证队列池中的元素数量是否正确
  size_t elements_count = cqueue_count(q->queue);
  if (elements_count != size) {
    kprintf(
        "Incorrect number of elements in the queue pool: expected %d, actual "
        "%d\n",
        size, elements_count);
  }

  // 从队列池中取出元素并验证
  for (int i = 0; i < poll_size; i++) {
    void* element = queue_pool_poll(q);
    if (element == NULL) {
      kprintf("Failed to poll element %d from the queue pool\n", i);
    } else {
      // 这里假设元素是整数，为了简化测试
      int value = *((int*)element);
      if (value != i) {
        kprintf("Polled element has incorrect value: expected %d, actual %d\n",
                i, value);
      }
      // kfree(element);
    }
  }

  // 验证队列池中的元素数量是否正确
  elements_count = cqueue_count(q->queue);
  if (elements_count != 0) {
    kprintf(
        "Incorrect number of elements in the queue pool after polling: "
        "expected 0, actual %d\n",
        elements_count);
  }

  // 销毁队列池
  queue_pool_destroy(q);

  kprintf("Test case completed\n");
}

void test_queue_pool() {
  // 测试用例 1: 创建队列池，并使用默认对齐方式
  run_test_queue_pool_case(10, 10, sizeof(int), 0);

  // 测试用例 2: 创建队列池，并指定对齐方式
  run_test_queue_pool_case(10, 10, sizeof(int), 16);

  // 测试用例 3: 测试空队列池的行为
  run_test_queue_pool_case(0, 0, sizeof(int), 0);

  // 测试用例 4: 测试队列池容量小于元素数量的情况
  run_test_queue_pool_case(1, 2, sizeof(int), 16);

  // 测试用例 6: 测试使用不同大小的元素和不同对齐方式
  run_test_queue_pool_case(8, 8, sizeof(float), 4);
  run_test_queue_pool_case(20, 20, sizeof(char), 1);
  run_test_queue_pool_case(15, 15, sizeof(double), 8);
}

void test_queue_pool_page() {
  int num=1000;
  queue_pool_t* q =
      queue_pool_create_align(num, PAGE_SIZE, PAGE_SIZE);
  int count = 0;
  for (int i = 0; i < num; i++) {
    void* addr = queue_pool_poll(q);
    if (addr != NULL) {
      kmemset(addr, 0, PAGE_SIZE);
      count++;
    } else {
      kprintf("Test case %d Failed\n", i);
    }
  }
  kprintf("test success %d\n", count);
}

// 内核测试用例
void test_kernel() {
  // test_pool();
  // test_queue_pool();
  // test_queue_pool_page();
}