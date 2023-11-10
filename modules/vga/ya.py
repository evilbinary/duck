# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("mod-vga")
set_kind("static")

add_deps(
    'kernel'
)

arch=get_arch()
arch_type=get_arch_type()
plat=get_plat()

plat_source={
    
}
arch_source={
  
}
common_source=[
    'vga.c',
    'mode.c',
    'qemu.c',
]


source=[]

if arch_source.get(arch_type):
    source+=arch_source.get(arch_type)

if plat_source.get(plat):
    source+=plat_source.get(plat)


add_files(
    source+common_source
)
