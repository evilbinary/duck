# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("mod-serial")
set_kind("static")

add_deps(
    'arch',
    'kernel',
)

arch=get_arch()
arch_type=get_arch_type()
plat=get_plat()

plat_source={
    'v3s':[
        'v3s.c',
    ],
    'raspi2':[
        'bcm2836.c',
    ],
    'raspi3':[
        'bcm2837.c',
    ],
    'rk3128':[
        'rk3128.c'
    ],
    'stm32':[
        'stm32.c'
    ],
    'dmulator':[
        'general.c'
    ],
    'riscv-virt':[
        'riscv-virt.c'
    ],
    'miyoo':[
        'ssd202d.c'
    ],
    't113-s3':[
        '16450.c'
    ],
    'f1c200s':[
        '16450.c'
    ],
    'versatilepb':[
        'pl011.c'
    ],
    'orangepi-pc':[
        '16450.c',
    ],
    'stm32f4xx':[
        'general.c'
    ]
}
arch_source={
    'arm': [

    ],
    'x86':[
        'serial.c'
    ]
}
common_source=[

]


source=[]

if arch_source.get(arch_type):
    source+=arch_source.get(arch_type)

if plat_source.get(plat):
    source+=plat_source.get(plat)


add_files(
    source+common_source
)


add_includedirs(
    '.',
    '../'
    )