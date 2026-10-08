# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("mod-gpu")
set_kind("static")

add_deps(
    'kernel',
    'mod-bcm',
    # v3s.c 等驱动直接 sysconf_get_int() 取 [lcd] 段配置
    'mod-sysconf',
    # YiYiYa: ssd202d.c 背光用 gpio_output（GPIO4）
    'mod-gpio'
)

arch=get_arch()
arch_type=get_arch_type()

def_arch=arch.replace("-", "_").upper()


def_arch_type=arch_type.replace( "-", "_").upper()

plat=get_plat()

plat_source={
    'v3s':[
        'v3s.c',
    ],
    'raspi2':[
        'bcm2836.c',
        'mailbox.c',
    ],
    'raspi3':[
        'bcm2836.c',
        'mailbox.c',
    ],
    # 【raspi5：BCM2712 + 双 HDMI】走固件 mailbox 路线（与 raspi2/3 同 IP，
    # 只是 mbox 基址挪到 0x107c013880）。mailbox.c 由 mod-bcm 提供，这里只编
    # bcm2712.c（它导出 gpu_init_mode_index 支持 /dev/fb0、/dev/fb1 双屏）。
    'raspi5':[
        'bcm2712.c',
    ],
    'rk3128':[
        'rk3128.c'
    ],
    'miyoo':[
        'ssd202d.c'
    ],
    't113-s3':[
        't113-s3.c'
    ],
    'orangepi-pc':[
        'h3.c'
    ],
    'versatilepb':[
        'pl110.c'
    ],
    'f1c200s':[
        'f1c200s.c'
    ]
}
arch_source={
    'arm':[
        'gpu.c',
    ],
    'arm64':[
        'gpu.c',
    ]
}


source=[]

if arch_source.get(arch_type):
    source+=arch_source.get(arch_type)

if plat_source.get(plat):
    source+=plat_source.get(plat)


add_files(
    source
)

add_includedirs(
    '.',
    '../',
    '../libs/include',
    '../platform',
    '../kernel',
    '../../',
    '../platform/{plat}'
)

add_defines(def_arch)
add_defines(def_arch_type)

# 【双屏】raspi5 有两路 HDMI（hdmi0@7c701400 / hdmi1@7c706400，DTB 实测），
# 注册 /dev/fb0 与 /dev/fb1 两个节点。其它平台保持单屏（/dev/fb）。
if plat == 'raspi5':
    add_defines('GPU_DISPLAY_COUNT=2')
