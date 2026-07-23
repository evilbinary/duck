/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * X Window System - Built-in Fonts
 ********************************************************************/
#ifndef XWIN_FONT_H
#define XWIN_FONT_H

#include "types.h"

// ========== 字体大小定义 ==========
#define FONT_WIDTH_8   8
#define FONT_HEIGHT_8  8

#define FONT_WIDTH_16  8
#define FONT_HEIGHT_16 16

// ========== 8x8 字体 (用于小尺寸显示) ==========
extern const u8 font8x8[128][8];

// ========== 8x16 字体 (默认字体) ==========
extern const u8 font8x16[128][16];

// ========== 字体 API ==========
u32 xfont_get_width(u32 size);
u32 xfont_get_height(u32 size);
const u8* xfont_get_glyph(char ch, u32 size);

#endif
