#ifndef V3S_H
#define V3S_H

#include "kernel/kernel.h"
#include "libs/include/types.h"


typedef struct v3s_lcd{
    u32 de;
	u32 tcon;

	int rstde;
	int rsttcon;
	int width;
	int height;

	int bits_per_pixel;
	int bytes_per_pixel;
	int index;
	void * vram[2];

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
	} timing;

	/* TCON0_CTRL[8:4] 像素时钟相位 CLK_DELAY(0..31)：老代码按消隐凑出
	 * (vfp+vbp+vspw)/2（800x480 那套 = 22）；/conf/system.conf 的 [lcd] clk_delay
	 * 可以覆盖它（0 是合法值，所以用 0xFFFFFFFF 当"没配"的哨兵）。 */
	int clk_delay;

	struct led_t * backlight;
	int brightness;
}v3s_lcd_t;



#endif
