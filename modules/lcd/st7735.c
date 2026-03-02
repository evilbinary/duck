/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "st7735.h"

#include "lcd.h"
#include "platform/stm32f4xx/gpio.h"
#include "platform/stm32f4xx/stm32f4xx_hal.h"

#define USE_BUFF 0

#define BLACK 0x0000
#define WHITE 0xFFFF

#define RED 0xf800
#define BLUE 0x001f
#define GREEN 0x07e0
#define YELLOW 0xffe0
#define MAGENTA 0xF81F
#define CYAN 0xFFE0

device_t* spi_dev = NULL;

#ifdef USE_BUFF
#define ST77XX_BUF_SIZE 2048
static uint8_t st7735_buf[ST77XX_BUF_SIZE];
static uint16_t st7735_buf_index = 0;

static void st77xx_write_buffer(u8* buff, size_t buff_size) {
  while (buff_size--) {
    st7735_buf[st7735_buf_index++] = *buff++;
    if (st7735_buf_index == ST77XX_BUF_SIZE) {
      spi_dev->write(spi_dev, st7735_buf, st7735_buf_index);
      st7735_buf_index = 0;
    }
  }
}

static void st77xx_flush_buffer(void) {
  if (st7735_buf_index > 0) {
    spi_dev->write(spi_dev, st7735_buf, st7735_buf_index);
    st7735_buf_index = 0;
  }
}
#endif

void delay(int n) {
  // Use simple loop delay - ~1ms per 10000 iterations at 84MHz
  for (volatile int i = 0; i < 10000 * n; i++) {
    __asm__ volatile("nop");
  }
}

// Debug: toggle CS to verify GPIO works
void st7735_debug_gpio() {
  kprintf("Testing GPIO...\n");
  for(int i = 0; i < 5; i++) {
    st7735_select();
    delay(10);
    st7735_unselect();
    delay(10);
  }
  kprintf("GPIO test done\n");
}

// ST7735S offset for 128x128 display - set to 0 if not needed
#define COL_OFFSET 0
#define ROW_OFFSET 0

void st7735_fill(u16 xsta, u16 ysta, u16 xend, u16 yend, u16 color) {
  u32 pixel_count = (xend - xsta + 1) * (yend - ysta + 1);

  // Apply offset correction for ST7735S
  // st7735_address_set sends 0x2C and keeps CS low
  st7735_address_set(xsta + COL_OFFSET, ysta + ROW_OFFSET, xend + COL_OFFSET, yend + ROW_OFFSET);

  // Set DC high for pixel data
  gpio_output(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_SET);

  // Write all pixels
  u8 color_bytes[2];
  color_bytes[0] = (color >> 8) & 0xFF;  // High byte
  color_bytes[1] = color & 0xFF;         // Low byte

#ifdef USE_BUFF
  for (u32 k = 0; k < pixel_count; k++) {
    st77xx_write_buffer(color_bytes, 2);
  }
  st77xx_flush_buffer();
#else
  for (u32 k = 0; k < pixel_count; k++) {
    spi_dev->write(spi_dev, color_bytes, 2);
  }
#endif

  // Release CS after all pixel data is sent
  st7735_unselect();
}

void st7735_set_pixel(u16 x, u16 y, u16 color) {
  // Apply offset correction
  u16 x_offset = x + COL_OFFSET;
  u16 y_offset = y + ROW_OFFSET;
  
  st7735_select();

  // Column Address Set
  gpio_output(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_RESET);
  u8 cmd = 0x2a;
  spi_dev->write(spi_dev, &cmd, 1);

  gpio_output(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_SET);
  u8 col_addr[4] = {(x_offset >> 8) & 0xFF, x_offset & 0xFF, (x_offset >> 8) & 0xFF, x_offset & 0xFF};
  spi_dev->write(spi_dev, col_addr, 4);

  // Row Address Set
  gpio_output(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_RESET);
  cmd = 0x2b;
  spi_dev->write(spi_dev, &cmd, 1);

  gpio_output(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_SET);
  u8 row_addr[4] = {(y_offset >> 8) & 0xFF, y_offset & 0xFF, (y_offset >> 8) & 0xFF, y_offset & 0xFF};
  spi_dev->write(spi_dev, row_addr, 4);

  // Memory Write
  gpio_output(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_RESET);
  cmd = 0x2c;
  spi_dev->write(spi_dev, &cmd, 1);

  // Write pixel data
  gpio_output(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_SET);
  u8 color_bytes[2];
  color_bytes[0] = (color >> 8) & 0xFF;
  color_bytes[1] = color & 0xFF;
  spi_dev->write(spi_dev, color_bytes, 2);

  st7735_unselect();
}

void st7735_address_set(u16 x1, u16 y1, u16 x2, u16 y2) {
  // Send entire address sequence without toggling CS
  st7735_select();  // CS low

  // Column Address Set (0x2A)
  gpio_output(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_RESET);  // DC low
  u8 cmd = 0x2a;
  spi_dev->write(spi_dev, (u8*)&cmd, 1);

  gpio_output(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_SET);  // DC high
  u8 col_addr[4] = {(x1 >> 8) & 0xFF, x1 & 0xFF, (x2 >> 8) & 0xFF, x2 & 0xFF};
  spi_dev->write(spi_dev, col_addr, 4);

  // Row Address Set (0x2B)
  gpio_output(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_RESET);  // DC low
  cmd = 0x2b;
  spi_dev->write(spi_dev, (u8*)&cmd, 1);

  gpio_output(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_SET);  // DC high
  u8 row_addr[4] = {(y1 >> 8) & 0xFF, y1 & 0xFF, (y2 >> 8) & 0xFF, y2 & 0xFF};
  spi_dev->write(spi_dev, row_addr, 4);

  // Memory Write (0x2C) - keep CS low for pixel data
  gpio_output(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_RESET);  // DC low
  cmd = 0x2c;
  spi_dev->write(spi_dev, (u8*)&cmd, 1);

  // CS stays low, will be released after pixel data
  // DC will be set high before sending pixels
}

void st7735_select() {
  gpio_output(ST7735_CS_GPIO_Port, ST7735_CS_Pin, GPIO_PIN_RESET);
}

void st7735_unselect() {
  gpio_output(ST7735_CS_GPIO_Port, ST7735_CS_Pin, GPIO_PIN_SET);
}

void st7735_reset() {
  // ST7735S Reset Sequence
  // Make sure CS is high during reset
  st7735_unselect();
  delay(5);
  
  // RES = 1 -> delay 10ms -> RES = 0 -> delay 10ms -> RES = 1 -> delay 120ms
  gpio_output(ST7735_RES_GPIO_Port, ST7735_RES_Pin, GPIO_PIN_SET);
  delay(10);
  
  // RES = 0 (reset low)
  gpio_output(ST7735_RES_GPIO_Port, ST7735_RES_Pin, GPIO_PIN_RESET);
  delay(10);
  
  // RES = 1 (release reset)
  gpio_output(ST7735_RES_GPIO_Port, ST7735_RES_Pin, GPIO_PIN_SET);
  delay(150);  // Wait 150ms after reset (some modules need longer)
}

// Internal function: send command without CS control
static void st7735_write_cmd_no_cs(u8 cmd) {
  gpio_output(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_RESET);  // DC low for command
  spi_dev->write(spi_dev, (u8*)&cmd, 1);
}

// Internal function: send data without CS control
static void st7735_write_data_no_cs(u8 data) {
  gpio_output(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_SET);  // DC high for data
  spi_dev->write(spi_dev, &data, 1);
}

// For standalone commands (with CS control)
void st7735_write_cmd(u8 cmd) {
  st7735_select();  // CS low
  st7735_write_cmd_no_cs(cmd);
  st7735_unselect();  // CS high
}

// Write data byte (assumes CS already low from previous command)
void st7735_write_data(u8 data) {
  gpio_output(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_SET);
  spi_dev->write(spi_dev, &data, 1);
}

// Read LCD ID (0x04 command)
u32 st7735_read_id() {
  u8 cmd = 0x04;
  u8 id[4] = {0};
  
  st7735_select();
  

  // Send command
  gpio_output(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_RESET);
  spi_dev->write(spi_dev, &cmd, 1);
  
  // Read 4 bytes (dummy + ID1 + ID2 + ID3)
  gpio_output(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_PIN_SET);
  // Note: spi_dev->read might not be implemented, try write then read
  
  st7735_unselect();
  
  return (id[1] << 16) | (id[2] << 8) | id[3];
}

// Debug: test SPI by toggling CS rapidly and sending data
void st7735_debug_spi() {
  kprintf("Testing SPI...\n");
  
  // Test 1: Send single byte
  u8 test_data = 0x55;
  int ret = spi_dev->write(spi_dev, &test_data, 1);
  kprintf("SPI write test: ret=%d\n", ret);
  
  // Test 2: Send multiple bytes with CS toggle
  st7735_select();
  u8 data2[4] = {0x01, 0x02, 0x03, 0x04};
  ret = spi_dev->write(spi_dev, data2, 4);
  st7735_unselect();
  kprintf("SPI multi-byte test: ret=%d\n", ret);
  
  kprintf("SPI test done\n");
}

void st7735_test() {
    // Fill screen with different colors (0-127 is valid range for 128x128 display)
    kprintf("Filling WHITE...\n");
    st7735_fill(0, 0, 127, 127, WHITE);
    delay(1000);
    
    kprintf("Filling BLUE...\n");
    st7735_fill(0, 0, 127, 127, BLUE);
    delay(1000);
    
    kprintf("Filling GREEN...\n");
    st7735_fill(0, 0, 127, 127, GREEN);
    delay(1000);
    
    kprintf("Filling RED...\n");
    st7735_fill(0, 0, 127, 127, RED);
    delay(1000);
    
    kprintf("st7735 test lcd end\n");
}

void st7735_init() {
  /*
   * STM32F4 ST7735 LCD连接配置
   * 
   * GPIO控制引脚：
   *   PA11  - DC  (数据/命令选择)
   *   PB7   - RES (复位)
   *   PA8   - CS  (片选，手动控制)
   * 
   * SPI1引脚 (硬件SPI)：
   *   PA5   - SCK  (时钟)
   *   PA7   - MOSI (数据输出)
   *   PB6   - MISO (未使用，仅用于SPI全双工)
   *   PA4   - NSS  (硬件片选，未使用)
   * 
   * 电源：
   *   VCC   - 3.3V
   *   GND   - GND
   *   LED   - 3.3V (背光，可接PWM调光)
   * 
   * 注意：CS使用PA8而不是硬件NSS(PA4)，避免与MISO(PB6)冲突
   */
  
  kprintf("st7735_init start\n");

  gpio_config(ST7735_DC_GPIO_Port, ST7735_DC_Pin, GPIO_MODE_OUTPUT_PP);
  gpio_config(ST7735_RES_GPIO_Port, ST7735_RES_Pin, GPIO_MODE_OUTPUT_PP);
  gpio_config(ST7735_CS_GPIO_Port, ST7735_CS_Pin, GPIO_MODE_OUTPUT_PP);
  
  // Set CS high initially
  st7735_unselect();
  delay(20);

  // init spi
  spi_dev = device_find(DEVICE_SPI);
  if (spi_dev == NULL) {
    kprintf("ERROR: SPI device not found!\n");
    return;
  }
  kprintf("SPI device found: %s\n", spi_dev->name);

  // Test GPIO
  st7735_debug_gpio();

  // Test SPI
  st7735_debug_spi();

  // init lcd - hardware reset
  kprintf("Doing hardware reset...\n");
  st7735_reset();
  
  // Extra delay after reset
  delay(200);

  //--------------------------------ST7735S Init Sequence--------------------------------
  kprintf("Starting ST7735S init sequence...\n");
  
  // Check CS and DC pin states before init
  kprintf("Before init - CS=%d, DC=%d\n", 
    HAL_GPIO_ReadPin(ST7735_CS_GPIO_Port, ST7735_CS_Pin),
    HAL_GPIO_ReadPin(ST7735_DC_GPIO_Port, ST7735_DC_Pin));
  
  // Sleep out
  kprintf("Sending 0x11 Sleep Out...\n");
  st7735_select();
  kprintf("After select - CS=%d\n", HAL_GPIO_ReadPin(ST7735_CS_GPIO_Port, ST7735_CS_Pin));
  st7735_write_cmd_no_cs(0x11); // Sleep out
  st7735_unselect();
  delay(120); // Delay 120ms
  kprintf("Sleep Out done\n");
  
  //--------------------------------ST7735S Frame Rate--------------------------------
  st7735_select();
  st7735_write_cmd_no_cs(0xB1);
  st7735_write_data_no_cs(0x05);
  st7735_write_data_no_cs(0x3C);
  st7735_write_data_no_cs(0x3C);
  st7735_unselect();
  
  st7735_select();
  st7735_write_cmd_no_cs(0xB2);
  st7735_write_data_no_cs(0x05);
  st7735_write_data_no_cs(0x3C);
  st7735_write_data_no_cs(0x3C);
  st7735_unselect();
  
  st7735_select();
  st7735_write_cmd_no_cs(0xB3);
  st7735_write_data_no_cs(0x05);
  st7735_write_data_no_cs(0x3C);
  st7735_write_data_no_cs(0x3C);
  st7735_write_data_no_cs(0x05);
  st7735_write_data_no_cs(0x3C);
  st7735_write_data_no_cs(0x3C);
  st7735_unselect();
  //--------------------------------End ST7735S Frame Rate--------------------------------
  
  st7735_select();
  st7735_write_cmd_no_cs(0xB4); // Dot inversion
  st7735_write_data_no_cs(0x03);
  st7735_unselect();
  
  //--------------------------------ST7735S Power Sequence--------------------------------
  st7735_select();
  st7735_write_cmd_no_cs(0xC0);
  st7735_write_data_no_cs(0x28);
  st7735_write_data_no_cs(0x08);
  st7735_write_data_no_cs(0x04);
  st7735_unselect();
  
  st7735_select();
  st7735_write_cmd_no_cs(0xC1);
  st7735_write_data_no_cs(0xC0);
  st7735_unselect();
  
  st7735_select();
  st7735_write_cmd_no_cs(0xC2);
  st7735_write_data_no_cs(0x0D);
  st7735_write_data_no_cs(0x00);
  st7735_unselect();
  
  st7735_select();
  st7735_write_cmd_no_cs(0xC3);
  st7735_write_data_no_cs(0x8D);
  st7735_write_data_no_cs(0x2A);
  st7735_unselect();
  
  st7735_select();
  st7735_write_cmd_no_cs(0xC4);
  st7735_write_data_no_cs(0x8D);
  st7735_write_data_no_cs(0xEE);
  st7735_unselect();
  //--------------------------------End ST7735S Power Sequence--------------------------------
  
  st7735_select();
  st7735_write_cmd_no_cs(0xC5); // VCOM
  st7735_write_data_no_cs(0x1A);
  st7735_unselect();
  
  // 65k mode - set BEFORE 0x36
  st7735_select();
  st7735_write_cmd_no_cs(0x3A); // 65k mode
  st7735_write_data_no_cs(0x05); // 16-bit color
  st7735_unselect();
  
  st7735_select();
  st7735_write_cmd_no_cs(0x36); // MX, MY, RGB mode
  st7735_write_data_no_cs(0xC8); // Try 0xC8 (BGR) instead of 0xC0 (RGB)
  st7735_unselect();
  
  //--------------------------------ST7735S Gamma Sequence--------------------------------
  st7735_select();
  st7735_write_cmd_no_cs(0xE0);
  st7735_write_data_no_cs(0x04);
  st7735_write_data_no_cs(0x22);
  st7735_write_data_no_cs(0x07);
  st7735_write_data_no_cs(0x0A);
  st7735_write_data_no_cs(0x2E);
  st7735_write_data_no_cs(0x30);
  st7735_write_data_no_cs(0x25);
  st7735_write_data_no_cs(0x2A);
  st7735_write_data_no_cs(0x28);
  st7735_write_data_no_cs(0x26);
  st7735_write_data_no_cs(0x2E);
  st7735_write_data_no_cs(0x3A);
  st7735_write_data_no_cs(0x00);
  st7735_write_data_no_cs(0x01);
  st7735_write_data_no_cs(0x03);
  st7735_write_data_no_cs(0x13);
  st7735_unselect();
  
  st7735_select();
  st7735_write_cmd_no_cs(0xE1);
  st7735_write_data_no_cs(0x04);
  st7735_write_data_no_cs(0x16);
  st7735_write_data_no_cs(0x06);
  st7735_write_data_no_cs(0x0D);
  st7735_write_data_no_cs(0x2D);
  st7735_write_data_no_cs(0x26);
  st7735_write_data_no_cs(0x23);
  st7735_write_data_no_cs(0x27);
  st7735_write_data_no_cs(0x27);
  st7735_write_data_no_cs(0x25);
  st7735_write_data_no_cs(0x2D);
  st7735_write_data_no_cs(0x3B);
  st7735_write_data_no_cs(0x00);
  st7735_write_data_no_cs(0x01);
  st7735_write_data_no_cs(0x04);
  st7735_write_data_no_cs(0x13);
  st7735_unselect();
  //--------------------------------End ST7735S Gamma Sequence--------------------------------
  
  // Normal Display Mode ON
  st7735_select();
  st7735_write_cmd_no_cs(0x13);
  st7735_unselect();
  delay(10);
  
  // Display Inversion OFF (some modules need this)
  st7735_select();
  st7735_write_cmd_no_cs(0x20);
  st7735_unselect();
  
  // Memory Data Access Control - refresh direction
  st7735_select();
  st7735_write_cmd_no_cs(0x36);
  st7735_write_data_no_cs(0xC8); // MX=1, MY=1, BGR=1
  st7735_unselect();
  
  st7735_select();
  st7735_write_cmd_no_cs(0x29); // Display on
  st7735_unselect();
  delay(100);
  
  // Set full screen address range
  // Column address: 0x00-0x7F (128 pixels)
  st7735_select();
  st7735_write_cmd_no_cs(0x2A);
  st7735_write_data_no_cs(0x00);
  st7735_write_data_no_cs(0x00);  // X start = 0
  st7735_write_data_no_cs(0x00);
  st7735_write_data_no_cs(0x7F);  // X end = 127
  st7735_unselect();
  
  // Row address: 0x00-0x7F (128 pixels)
  st7735_select();
  st7735_write_cmd_no_cs(0x2B);
  st7735_write_data_no_cs(0x00);
  st7735_write_data_no_cs(0x00);  // Y start = 0
  st7735_write_data_no_cs(0x00);
  st7735_write_data_no_cs(0x7F);  // Y end = 127
  st7735_unselect();
  
  delay(100);

  kprintf("st7735 lcd init done, starting test...\n");

  // First, test with a single pixel
  kprintf("Setting single pixel at (10,10)...\n");
  st7735_set_pixel(10, 10, RED);
  delay(500);
  
  // Try to fill a small area
  kprintf("Filling small area (20,20) to (30,30)...\n");
  st7735_fill(20, 20, 30, 30, GREEN);
  delay(1000);
  
  // Fill entire screen
  kprintf("Filling entire screen RED...\n");
  st7735_fill(0, 0, 127, 127, RED);
  delay(2000);
  
  while(1){
    st7735_test();
  }
}

int st7735_write_pixel(vga_device_t* vga, const void* buf, size_t len) {
  u16* color = buf;
  int i = 0;
  for (i = 0; i < len / 6; i += 3) {
    st7735_set_pixel(color[i], color[i + 1], color[i + 2]);
  }
  return i;
}

int lcd_init_mode(vga_device_t* vga, int mode) {
  if (mode == VGA_MODE_128x128x16) {
    vga->width = 128;
    vga->height = 128;
    vga->bpp=16;
  } else {
    kprintf("lcd no support mode %x\n");
  }
  vga->mode = mode;
  vga->write = st7735_write_pixel;
  // vga->flip_buffer=gpu_flush;

  vga->framebuffer_index = 0;
  vga->framebuffer_count = 1;
  vga->frambuffer = NULL;
  vga->pframbuffer = vga->frambuffer;

  st7735_init();

  return 0;
}