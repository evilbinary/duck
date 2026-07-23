/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "lcd.h"

#include "dev/devfs.h"
#include "xwin/font.h"
#include "vga/vga.h"
#include "kernel/string.h"

static inline u32 lcd_text_scale(u32 size) {
  if (size == 8) {
    return 1;
  }
  size /= 16;
  if (size < 1) {
    size = 1;
  }
  return size;
}

static inline void lcd_put_pixel(int x, int y, u16 color) {
  extern void lcd_set_pixel(u16 x, u16 y, u16 color);
  if (x < 0 || y < 0) {
    return;
  }
  lcd_set_pixel((u16)x, (u16)y, color);
}

static void lcd_draw_line(int x0, int y0, int x1, int y1, u16 color) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  int sx = x0 < x1 ? 1 : -1;
  int dy = y1 > y0 ? -(y1 - y0) : -(y0 - y1);
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  for (;;) {
    lcd_put_pixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    int e2 = err << 1;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

static void lcd_draw_rect(int x, int y, int w, int h, u16 color) {
  if (w <= 0 || h <= 0) {
    return;
  }
  lcd_draw_line(x, y, x + w - 1, y, color);
  lcd_draw_line(x, y, x, y + h - 1, color);
  lcd_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
  lcd_draw_line(x, y + h - 1, x + w - 1, y + h - 1, color);
}

static void lcd_draw_char(int x, int y, char ch, u16 color, u16 bg_color,
                          int draw_bg, u32 size) {

  const u8* glyph = xfont_get_glyph(ch, size);
  if (glyph == NULL) {
    return;
  }

  u32 height = xfont_get_height(size);
  u32 scale = lcd_text_scale(size);

  for (u32 row = 0; row < height; row++) {
    u8 bits = glyph[row];
    for (u32 col = 0; col < 8; col++) {
      int set = (bits & (0x80 >> col)) != 0;
      if (set || draw_bg) {
        u16 pixel = set ? color : bg_color;
        for (u32 sy = 0; sy < scale; sy++) {
          for (u32 sx = 0; sx < scale; sx++) {
            lcd_put_pixel(x + col * scale + sx, y + row * scale + sy, pixel);
          }
        }
      }
    }
  }
}

static void lcd_draw_text(int x, int y, const char* text, u16 color, u32 size) {
  if (text == NULL) {
    return;
  }

  u32 scale = lcd_text_scale(size);
  int cx = x;
  while (*text != 0) {
    lcd_draw_char(cx, y, *text, color, 0, 0, size);
    cx += 8 * scale;
    text++;
  }
}

static void lcd_draw_text_bg(int x, int y, const char* text, u16 color,
                             u16 bg_color, u32 size) {
  if (text == NULL) {
    return;
  }

  u32 scale = lcd_text_scale(size);
  int cx = x;
  while (*text != 0) {
    lcd_draw_char(cx, y, *text, color, bg_color, 1, size);
    cx += 8 * scale;
    text++;
  }
}

size_t lcd_read(device_t* dev, void* buf, size_t len) {
  u32 ret = 0;
  return ret;
}

size_t lcd_write(device_t* dev, const void* buf, size_t len) {
  u32 ret = 0;
  vga_device_t* vga = dev->data;
  if (vga == NULL) {
    kprintf("not found lcd\n");
    return ret;
  }
  
  // 解析FILL命令: "FILL x y w h color\n"
  const char* cmd = (const char*)buf;
  if (kstrncmp(cmd, "FILL ", 5) == 0) {
    // 简单解析5个整数
    char* p = (char*)cmd + 5;
    int x = katoi((const char**)&p);
    while (*p == ' ') p++;
    int y = katoi((const char**)&p);
    while (*p == ' ') p++;
    int w = katoi((const char**)&p);
    while (*p == ' ') p++;
    int h = katoi((const char**)&p);
    while (*p == ' ') p++;
    int color = katoi((const char**)&p);
    
    // 调用st7735_fill填充矩形
    lcd_fill(x, y, x + w - 1, y + h - 1, color);
    return len;
  }

  if (kstrncmp(cmd, "PIXEL ", 6) == 0) {
    char* p = (char*)cmd + 6;
    int x = katoi((const char**)&p);
    while (*p == ' ') p++;
    int y = katoi((const char**)&p);
    while (*p == ' ') p++;
    int color = katoi((const char**)&p);
    lcd_put_pixel(x, y, (u16)color);
    return len;
  }

  if (kstrncmp(cmd, "LINE ", 5) == 0) {
    char* p = (char*)cmd + 5;
    int x1 = katoi((const char**)&p);
    while (*p == ' ') p++;
    int y1 = katoi((const char**)&p);
    while (*p == ' ') p++;
    int x2 = katoi((const char**)&p);
    while (*p == ' ') p++;
    int y2 = katoi((const char**)&p);
    while (*p == ' ') p++;
    int color = katoi((const char**)&p);
    lcd_draw_line(x1, y1, x2, y2, (u16)color);
    return len;
  }

  if (kstrncmp(cmd, "RECT ", 5) == 0) {
    char* p = (char*)cmd + 5;
    int x = katoi((const char**)&p);
    while (*p == ' ') p++;
    int y = katoi((const char**)&p);
    while (*p == ' ') p++;
    int w = katoi((const char**)&p);
    while (*p == ' ') p++;
    int h = katoi((const char**)&p);
    while (*p == ' ') p++;
    int color = katoi((const char**)&p);
    lcd_draw_rect(x, y, w, h, (u16)color);
    return len;
  }

  if (kstrncmp(cmd, "TEXT ", 5) == 0) {
    char* p = (char*)cmd + 5;
    int x = katoi((const char**)&p);
    while (*p == ' ') p++;
    int y = katoi((const char**)&p);
    while (*p == ' ') p++;
    int color = katoi((const char**)&p);
    while (*p == ' ') p++;
    int size = katoi((const char**)&p);
    while (*p == ' ') p++;

    lcd_draw_text(x, y, p, (u16)color, (u32)size);
    return len;
  }

  if (kstrncmp(cmd, "TEXTBG ", 7) == 0) {
    char* p = (char*)cmd + 7;
    int x = katoi((const char**)&p);
    while (*p == ' ') p++;
    int y = katoi((const char**)&p);
    while (*p == ' ') p++;
    int color = katoi((const char**)&p);
    while (*p == ' ') p++;
    int bg_color = katoi((const char**)&p);
    while (*p == ' ') p++;
    int size = katoi((const char**)&p);
    while (*p == ' ') p++;

    lcd_draw_text_bg(x, y, p, (u16)color, (u16)bg_color, (u32)size);
    return len;
  }
  
  if (vga->frambuffer != NULL) {
    kstrncpy(vga->frambuffer, (const char*)buf, len);
  } else {
    vga->write(vga, buf, len);
  }
  return ret;
}

size_t lcd_ioctl(device_t* dev, u32 cmd, void* args) {
  u32 ret = 0;
  vga_device_t* vga = dev->data;
  if (vga == NULL) {
    kprintf("not found lcd\n");
    return ret;
  }
  if (cmd == VGA_IOC_READ_FRAMBUFFER) {
    ret = (u32)(uintptr_t)vga->frambuffer;
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_WIDTH) {
    ret = vga->width;
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_HEIGHT) {
    ret = vga->height;
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_BPP) {
    ret = vga->bpp;
  } else if (cmd == VGA_IOC_FLUSH_FRAMBUFFER) {
    if (vga->frambuffer != NULL && vga->flip_buffer != NULL) {
      u32 offset = (u32)(uintptr_t)args;
      vga->flip_buffer(vga, offset % vga->framebuffer_count);
    }
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_INFO) {
    kprintf("lcd read framebuffer info\n");
    vga_device_t* buffer_info = (u32*)args;
    u32 size = (u32*)args;
    *buffer_info = *vga;
  }
  return ret;
}

void lcd_init_device(device_t* dev) {
  vga_device_t* vga = kmalloc(sizeof(vga_device_t), DEFAULT_TYPE);
  vga->frambuffer = 0;
  dev->data = vga;
  lcd_init_mode(vga, VGA_MODE_128x128x16);
  kprintf("lcd_init_device end\n");
}

int lcd_init(void) {
  kprintf("lcd_init\n");
  device_t* dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  dev->name = "lcd";
  dev->read = lcd_read;
  dev->write = lcd_write;
  dev->ioctl = lcd_ioctl;
  dev->id = DEVICE_LCD;
  dev->type = DEVICE_TYPE_VGA;
  device_add(dev);

  if (dev != NULL) {
    vnode_t* frambuffer = vfs_create_node("lcd", V_FILE);
    vfs_mount(NULL, "/dev", frambuffer);
    frambuffer->device = dev;
    frambuffer->op = &device_operator;
  } else {
    log_error("dev lcd not found\n");
  }

  lcd_init_device(dev);
  return 0;
}

void lcd_exit(void) { kprintf("lcd exit\n"); }

module_t lcd_module = {.name = "vga", .init = lcd_init, .exit = lcd_exit};