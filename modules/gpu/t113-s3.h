#ifndef T113_S3_H
#define T113_S3_H

#include "kernel/kernel.h"
#include "libs/include/types.h"


typedef struct t113_s3_lcd {
  u32 de;
  u32 tcon;

  u32 clk_tconlcd;
  int rst_de;
  int rst_tconlcd;
  int width;
  int height;
  int pwidth;
  int pheight;
  int bits_per_pixel;
  int bytes_per_pixel;
  int pixlen;
  int index;
  void *vram[2];

  struct {
    int pixel_clock_hz;
    int h_front_porch;
    int h_back_porch;
    int h_sync_len;
    int v_front_porch;
    int v_back_porch;
    int v_sync_len;
    int h_sync_active;
    int v_sync_active;
    int den_active;
    int clk_active;

    int ht; //Horizontal Total time
    int x; //显示屏的水平像素数量
    int y; //显示屏的垂直行数
    int hbp; //Horizontal Back Porch
    int hfp;

    int hspw; //Horizontal Sync Pulse Width
    int vt; //Vertical Total time
    int vbp; //Vertical Back Porch
    int vfp;
    int vspw; //Vertical Sync Pulse Width
    int dclk_freq; //Dot Clock Frequency

  } timing;

  struct led_t *backlight;
  int brightness;
} t113_s3_lcd_t;

#endif
