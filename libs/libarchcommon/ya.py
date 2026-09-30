# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************

target("archcommon")

set_kind("static")

print('Arch type:', get_arch_type())

arch_type=get_arch_type()
plat=get_plat()

# GIC 驱动按控制器版本二选一（两者导出的函数同名，不能同时编）：
#   · 默认 arm64 → gic3.c（GICv3，需要 GICR 帧）
#   · raspi5：GIC-400 是 GICv2（真机 DTB compatible="arm,gic-400"），
#     用 arm/gic2.c 的通用 GICv2 驱动（gicv2_chip），与 v3s 同一条路。
if arch_type=='arm64' and plat=='raspi5':
    add_files(
                "./arm/gic2.c",
                "./arm64/io.c"
                )
else:
    if arch_type:
        add_files(
                    "./"+arch_type+"/*.c"
                    )

add_includedirs(
    '../include',
    '../include/archcommon'
)

