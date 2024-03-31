# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("mod-sdhci")
set_kind("static")

add_deps(
    'kernel',
    'mod-bcm'
)

arch=get_arch()
arch_type=get_arch_type()
plat=get_plat()

plat_source={
    'v3s':[
        'v3s.c',
        'sunxi-sdhci.c',
    ],
    'raspi2':[
        'bcm2836.c',
        'bcm2835_vc.c',
    ],
    'rk3128':[
        'rk3128.c'
    ],
    'stm32':[
        'stm32.c'
    ],
    'general':[
        'general.c'
    ],
    'miyoo':[
        'ssd202d.c',
        'ssd202d_sdmmc.c'
    ],
    't113-s3':[
        'sunxi-sdhci.c',
        't113-s3.c'
    ],
    'versatilepb':[
        'pl181.c'
    ],
    'orangepi-pc':[
        'h3.c',
        'sunxi-sdhci.c',
    ],
    'f1c200s':[
        'f1c200s.c',
         'sunxi-sdhci.c',
    ]
    
}
arch_source={
    'arm': [
        'sdhci.c'
    ]
}
common_source=[
    'i2c.c',
]


source=[]

if arch_source.get(arch_type):
    source+=arch_source.get(arch_type)

if plat_source.get(plat):
    source+=plat_source.get(plat)


add_files(
    source+common_source
)

add_includedirs('../')