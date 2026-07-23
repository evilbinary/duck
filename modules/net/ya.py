# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("mod-net")
set_kind("static")

add_deps(
    'kernel',
)

arch=get_arch()
arch_type=get_arch_type()
plat=get_plat()

# Platform-specific network drivers
plat_source={
    'raspi2':[
        'bcm2837.c',
    ],
    'raspi3':[
        'bcm2837.c',
    ],
    'dmulator':[
        'e1000.c',
    ],
    'qemu':[
        'e1000.c',
    ],
    'versatilepb':[
        'e1000.c',
    ],
}

# Architecture-specific drivers
arch_source={
    'x86': [
        'e1000.c',
    ],
    'arm':[
        # Add ARM-specific network drivers here
    ],
    'arm64':[
        # Add ARM64-specific network drivers here
    ]
}

# Common source files (always compiled)
common_source=[
    'net.c',
]

# Build source list
source=[]

# Add architecture-specific sources
if arch_source.get(arch_type):
    source+=arch_source.get(arch_type)

# Add platform-specific sources (overrides arch-specific)
if plat_source.get(plat):
    source+=plat_source.get(plat)

add_files(
    source+common_source
)

add_includedirs(
    '.',
    '../'
)
