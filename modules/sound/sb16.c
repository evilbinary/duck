/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "dev/devfs.h"
#include "dma/dma.h"
#include "kernel/kernel.h"

#define DSP_ADDR_PORT 0x224
#define DSP_DATA_PORT 0x225
#define DSP_RESET 0x226
#define DSP_READ 0x22A
#define DSP_WRITE 0x22C
#define DSP_STATUS 0x22E
#define DSP_INT_ACK 0x22F
#define DSP_ACK_8 DSP_STATUS
#define DSP_ACK_16 0x22F

#define MIXER_IRQ 0x25
#define MIXER_IRQ_DATA 0x2

#define DSP_SET_TIME 0x40
#define DSP_SET_RATE 0x41
#define DSP_SET_INPUT_RATE 0x42

#define DSP_ON 0xD1
#define DSP_OFF 0xD3
#define DSP_OFF_8 0xD0
#define DSP_ON_8 0xD4
#define DSP_OFF_16 0xD5
#define DSP_ON_16 0xD6
#define DSP_VERSION 0xE1

#define DSP_PROG_16 0xB0
#define DSP_PROG_8 0xC0
#define DSP_AUTO_INIT 0x06
#define DSP_PLAY 0x00
#define DSP_RECORD 0x08
#define DSP_MONO 0x00
#define DSP_STEREO 0x20
#define DSP_UNSIGNED 0x00
#define DSP_SIGNED 0x10

#define SAMPLE_RATE 44100

#define DSP_VOLUME 0x22
#define DSP_IRQ 5

#define DMA_CHANNEL_16 5
#define DMA_FLIP_FLOP 0xD8
#define DMA_BASE_ADDR 0xC4
#define DMA_COUNT 0xC6

#define BUFFER_MS 1000
#define BUFFER_SIZE ((size_t)(SAMPLE_RATE * (BUFFER_MS / 1000.0)))

#define PLAY_LEN 11024
// #define BUFFER_SIZE 1024 * 4

// u8 buffer[BUFFER_SIZE];
u8* buffer;
u32 buffer_index = 0;

void delay(u32 d) {
  for (int i = 0; i < d; i++)
    ;
}

void dsp_write(u8 value) {
  while (io_read8(DSP_WRITE) & 0x80)
    ;
  io_write8(DSP_WRITE, value);
}

u8 dsp_read() {
  while (!(io_read8(DSP_STATUS) & 0x80))
    ;
  return io_read8(DSP_READ);
}

void set_sample_rate(u16 hz) {
  dsp_write(DSP_SET_RATE);
  dsp_write((u8)((hz >> 8) & 0xFF));
  dsp_write((u8)(hz & 0xFF));
}

void set_sample_input_rate(u16 hz) {
  dsp_write(DSP_SET_INPUT_RATE);
  dsp_write((u8)((hz >> 8) & 0xFF));
  dsp_write((u8)(hz & 0xFF));
}

void dsp_reset() {
  io_write8(DSP_RESET, 1);
  delay(10000);
  io_write8(DSP_RESET, 0);

  if (dsp_read() != 0xAA) {
    log_error("init sb16 failed\n");
  }

  io_write8(DSP_WRITE, DSP_VERSION);
  u8 major = io_read8(DSP_READ);
  u8 minor = io_read8(DSP_READ);

  log_info("reset sb16 major %d minor %d\n", major, minor);
}

void* phy_addr(void* buf, int len) {
  size_t phys = buf;
  thread_t* current = thread_current();
  if (current != NULL) {
    phys = kpage_v2p(buf, len);  // DMA phy
  }
  return phys;
}

void sb16_play(void* buf, size_t len) {
  if (1) {
    // 00001010
    // Program ISA DMA to transfer
    // DMA channel 1
    io_write8(0x0A, 1 | 4);  // Disable channel 1
    io_write8(0x0C, 1);      // Flip-flop (any value e.g. 1)
    io_write8(0x0B, 0x49);   // Transfert mode (0x48 for single mode/0x58 for
                             // auto mode + channel number)
    size_t phys = phy_addr(buf, len);  // DMA phy

    io_write8(0x83, (phys >> 16) & 0xFF);
    io_write8(0x02, (phys >> 8) & 0xFF);
    io_write8(0x02, phys & 0xFF);
    io_write8(0x03, (len >> 8) & 0xFF);
    io_write8(0x03, len & 0xFF);
    io_write8(0x0A, 1);  // Ènable channel 1
  } else {
    // 11010100
    io_write8(0xD4, 1 | 4);  // Disable channel 5
    io_write8(0xD8, 1);      // Flip-flop (any value e.g. 1)
    io_write8(0xD6, 0x49);   // Transfert mode (0x48 for single mode/0x58 for
                             // auto mode + channel number)
    size_t phys = phy_addr(buf, len);  // DMA !
    io_write8(0x8B, (phys >> 16) & 0xFF);
    io_write8(0xC4, (phys >> 8) & 0xFF);
    io_write8(0xC4, phys & 0xFF);
    io_write8(0xC6, (len >> 8) & 0xFF);
    io_write8(0xC6, len & 0xFF);
    io_write8(0xD4, 1);  // Ènable channel 5
  }

  // Program sound blaster 16
  // io_write8(DSP_WRITE, DSP_SET_TIME);  // Set time constant
  // io_write8(DSP_WRITE, 0xee);   // Sample rate 10989 Hz

  // set rate
  // set_sample_rate(SAMPLE_RATE/2);
  set_sample_input_rate(SAMPLE_RATE / 2);

  // Transfer mode
  io_write8(DSP_WRITE, DSP_PROG_16);
  // Type of sound data
  io_write8(DSP_WRITE, DSP_STEREO | DSP_SIGNED);
  io_write8(DSP_WRITE, ((len - 1) >> 8) & 0xFF);
  io_write8(DSP_WRITE, (len - 1) & 0xFF);
}

static size_t read(device_t* dev, void* buf, size_t len) {
  u32 ret = 0;

  return ret;
}

static size_t write(device_t* dev, void* buf, size_t len) {
  u32 ret = len;

  sb16_play(buf, len);
  return ret;
}

void do_sb16(interrupt_context_t* context) {
  log_debug("do sb16 irq\n");

  // io_read8(DSP_STATUS);
  // io_read8(DSP_ACK_16);
  // io_read8(DSP_INT_ACK);

  pic_eof(MIXER_IRQ);
}

INTERRUPT_SERVICE
void sb16_handler(interrupt_context_t* context) {
  interrupt_entering_code(MIXER_IRQ, 0, 0);
  interrupt_process(do_sb16);
  interrupt_exit();
}

int sb16_init(void) {
  log_info("sb16 init\n");
  device_t* dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  dev->name = "sb";
  dev->read = read;
  dev->write = write;
  dev->id = DEVICE_SB;
  dev->type = DEVICE_TYPE_BLOCK;
  device_add(dev);

  // dsp
  vnode_t* dsp = vfs_create_node("dsp", V_FILE | V_BLOCKDEVICE);
  dsp->device = device_find(DEVICE_SB);
  dsp->op = &device_operator;
  vfs_mount(NULL, "/dev", dsp);

  interrupt_regist(MIXER_IRQ, sb16_handler);

  // set irq
  io_write8(DSP_ADDR_PORT, DSP_IRQ);
  io_write8(DSP_DATA_PORT, MIXER_IRQ_DATA);

  // reset
  dsp_reset();

  // buffer = kmalloc(BUFFER_SIZE, DEVICE_TYPE);

  set_sample_rate(SAMPLE_RATE);
  // sb16_play(buffer, BUFFER_SIZE);

  dsp_write(DSP_ON);
  dsp_write(DSP_ON_16);

  // pic_enable(MIXER_IRQ);
  return 0;
}

void sb16_exit(void) { kprintf("sb16 exit\n"); }

module_t sb16_module = {.name = "sb16", .init = sb16_init, .exit = sb16_exit};
