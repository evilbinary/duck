# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("mod-lcd")
set_kind("static")

add_deps(
    'kernel',
    'mod-dma'
)


arch=get_arch()
arch_type=get_arch_type()
plat=get_plat()

plat_source={
 
    'stm32':[
        'st7735.c'
    ],
    't113-s3':[
        'ili9488.c',
    ],
    'stm32f4xx':[
        'st7735.c'
    ],
    'v3s':[
        # 'ili9431.c'
        'st7789.c'
    ]
}
arch_source={
    'arm': [

    ],
    'x86':[

    ]
}
common_source=[
    'lcd.c',
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
