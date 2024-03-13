#ifndef __T113_S3_REG_TCON_H__
#define __T113_S3_REG_TCON_H__

#include "libs/include/types.h"


#define T113_TCONLCD_BASE		(0x05461000)
#define T113_TCON_BASE  T113_TCONLCD_BASE

struct t113_tconlcd_reg_t {
	u32 gctrl;				/* 0x00 */
	u32 gint0;				/* 0x04 */
	u32 gint1;				/* 0x08 */
	u32 res_0c;
	u32 frm_ctrl;				/* 0x10 */
	u32 frm_seed[6];			/* 0x14 */
	u32 frm_table[4];			/* 0x2c */
	u32 fifo_3d;				/* 0x3c */
	u32 ctrl;					/* 0x40 */
	u32 dclk;					/* 0x44 */
	u32 timing0;				/* 0x48 */
	u32 timing1;				/* 0x4c */
	u32 timing2;				/* 0x50 */
	u32 timing3;				/* 0x54 */
	u32 hv_intf;				/* 0x58 */
	u32 res_5c;
	u32 cpu_intf;				/* 0x60 */
	u32 cpu_wr;				/* 0x64 */
	u32 cpu_rd0;				/* 0x68 */
	u32 cpu_rd1;				/* 0x6c */
	u32 res_70_80[5];			/* 0x70 */
	u32 lvds_intf;			/* 0x84 */
	u32 io_polarity;			/* 0x88 */
	u32 io_tristate;			/* 0x8c */
	u32 res_90_f8[27];
	u32 debug;				/* 0xfc */
	u32 ceu_ctl;				/* 0x100 */
	u32 res_104_10c[3];
	u32 ceu_coef[20];			/* 0x110 */
	u32 cpu_tri0;				/* 0x160 */
	u32 cpu_tri1;				/* 0x164 */
	u32 cpu_tri2;				/* 0x168 */
	u32 cpu_tri3;				/* 0x16c */
	u32 cpu_tri4;				/* 0x170 */
	u32 cpu_tri5;				/* 0x174 */
	u32 res_178_17c[2];
	u32 cmap_ctl;				/* 0x180 */
	u32 res_184_18c[3];
	u32 cmap_odd0;			/* 0x190 */
	u32 cmap_odd1;			/* 0x194 */
	u32 cmap_even0;			/* 0x198 */
	u32 cmap_even1;			/* 0x19c */
	u32 res_1a0_1ec[20];
	u32 safe_period;			/* 0x1f0 */
	u32 res_1f4_21c[11];
	u32 lvds_ana0;			/* 0x220 */
	u32 lvds_ana1;			/* 0x224 */
	u32 res_228_22c[2];
	u32 sync_ctl;				/* 0x230 */
	u32 sync_pos;				/* 0x234 */
	u32 slave_stop_pos;		/* 0x238 */
	u32 res_23c_3fc[113];
	u32 gamma_table[256];		/* 0x400 */
};



#endif