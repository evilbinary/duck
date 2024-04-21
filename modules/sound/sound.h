
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

    char* sound_buf;
    int is_play;
    int play_size;
    int buf_pos;
}sound_device_t;

#endif



