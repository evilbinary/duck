# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("kernel.elf")
set_kind("binary")
add_deps(
    'modules',
    'kernel',
    'arch',
    'platform',
    'archcommon',
    'kernelcommon',
    'algorithm',
    'gcc'
)
add_files(
    'main.c',
    'module.c',
    'init.c',
    'monitor.c',
    'test.c'
)

arch=get_arch()
arch_type=get_arch_type()

if has_config('single-kernel'):
    get_build_obj_dir()
    add_defines('SINGLE_KERNEL')
    add_files(
            '../../boot/'+arch_type+'/boot-'+arch+'.s',
            '../../boot/'+arch_type+'/init.c')

add_includedirs(
    '../platform/{plat}',
    '../libs/include',
    '../',
    '../libs/include/archcommon'
)




def_arch=arch.replace("-", "_").upper()


def_arch_type=arch_type.replace( "-", "_").upper()

add_defines(def_arch)
add_defines(def_arch_type)

add_ldflags("-T"+path.join(os.scriptdir(), "../xlinker/link-{plat}.ld"),  force = True)


add_rules("kernel-objcopy")

target('boot-config')
add_deps('kernel.elf')

add_files(
    "{buildir}/kernel"
)
add_rules("kernel-gen")