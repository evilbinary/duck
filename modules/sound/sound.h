
/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef SOUND_H
#define SOUND_H

#include "kernel/kernel.h"


#define AFMT_U16_LE 16
#define AFMT_S16_LE 16
#define AFMT_S16_BE 16
#define AFMT_U8 8

#define SNDCTL_DSP_SETFMT 11
#define SNDCTL_DSP_GETFMTS 22
#define SNDCTL_DSP_CHANNELS 33
#define SNDCTL_DSP_SPEED 44
#define SNDCTL_DSP_SETFRAGMENT 55
#define SNDCTL_DSP_SETTRIGGER 66


typedef struct sound_device{
    void * buffer;
    char* sound_buf;
    int is_play;
    int play_size;
    int buf_pos;
    int channal;
    /* 【音频 DMA 通道】原来 dma_init/dma_trans/dma_stop 里硬编码 0。
     * 注意：LCD(st7789) 也用通道 0 ⇒ 两者的回调/描述符槽位
     * （sunxi-dma.c 的 dma_channel_source[ch]）互相覆盖：谁后初始化谁生效
     * ⇒ 音频中断可能拿到 LCD 的 data 指针、搬运被改写 ⇒ 一边刷屏一边出声时
     * 断续/爆音。只放音频、或走 xwin DIRECT（不触发 LCD DMA）时不冲突。
     * 想让音频与 LCD 隔离：把本字段置成 1（sound_init 里一处）。 */
    int dma_channel;
}sound_device_t;

#endif



