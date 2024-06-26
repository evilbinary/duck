#include "fat/fat.h"
#include "kernel/kernel.h"

#define PRINT_WIDTH 24
#define READ_BUFFER 24 * 20


#ifdef FAT_MODULE

void test_fat_read() {
  device_t* dev = device_find(DEVICE_SATA);
  if (dev == NULL) {
    log_error("test ahci port failed\n");
    return;
  }

  vnode_t* node = kmalloc(sizeof(vnode_t), DEFAULT_TYPE);
  node->device = dev;

  int offset = 0xFFFF000;
  int count = 2 * 512;
  char buf[1024];
  kmemset(buf, 0, 1024);

  int ret = fat_read_bytes(node, offset, count, buf);
  if (ret < 0) {
    log_error("test fat error %d\n", ret);
    return;
  }
  for (int i = 0; i < 1024; i++) {
    if (buf[i] != (i % 128)) {
      log_error("test error fat at pos %d\n", i);
      break;
    }
  }
}

void test_fat_read_small() {
  device_t* dev = device_find(DEVICE_SATA);
  if (dev == NULL) {
    log_error("test ahci port failed\n");
    return;
  }

  vnode_t* node = kmalloc(sizeof(vnode_t), DEFAULT_TYPE);
  node->device = dev;

  int offset = 0xFFFF000;
  char buf[READ_BUFFER];
  kmemset(buf, 0, READ_BUFFER);

  int ret = fat_read_bytes(node, offset, READ_BUFFER, buf);
  if (ret < 0) {
    log_error("test fat small error %d\n", ret);
    return;
  }
  for (int i = 0; i < READ_BUFFER; i++) {
    if (buf[i] != (i % 128)) {
      log_error("test error fat small at pos %d\n", i);
      break;
    }
  }
}
#endif

void test_fat_read_file() {
  vnode_t* node = vfs_find(NULL, "/dev/sda");
  if (node == NULL) {
    log_error("test read file failed\n");
    return;
  }
  vnode_t* duck = node->op->find(node, "duck.png");
  if (node == NULL) {
    log_error("test read file failed duck not found\n");
    return;
  }
  int offset = 0x2a0;
  char* buffer = kmalloc(READ_BUFFER, DEFAULT_TYPE);
  if (duck == NULL) {
    log_error("duck node is null\n");
    return;
  }
  
  int ret = duck->op->open(duck, 0);
  if (ret <= 0) {
    log_error("open <=0\n");
  }

  ret = duck->op->read(duck, offset, READ_BUFFER, buffer);
  if (ret <= 0) {
    log_error("read <=0\n");
  }


  if (buffer[0] != 0x19) {
    log_error("test read file error 1\n");
  }
  if (buffer[1] != 0x29) {
    log_error("test read file error 2\n");
  }
  if (buffer[2] != 0x4c) {
    log_error("test read file error 3\n");
  }
  if (buffer[384] != 0xff) {
    log_error("test read file error 4\n");
  }
  if (buffer[385] != 0x5f) {
    log_error("test read file error 5\n");
  }
}

void test_fat() {
#ifdef FAT_MODULE
  test_fat_read();
  test_fat_read_small();
#endif
  test_fat_read_file();
}
