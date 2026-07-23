# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("kernel")
set_kind("static")

add_deps("archcommon","arch")

# cflags='$CFLAGS -finstrument-functions  -finstrument-functions-exclude-function-list=do_page_fault,syscall_handler,general_protection,syscall1,syscall2,syscall3,syscall4,syscall5,print_string,do_shell_cmd,print_promot -finstrument-functions-exclude-file-list=duck/init,duck/arch,duck/libs,duck/init,duck/kernel/thread.c,duck/kernel/schedule.c,duck/kernel/syscall.c,duck/kernel/sysfn.c,duck/kernel/vfs.c,duck/kernel/devfn.c,duck/kernel/exceptions.c,duck/kernel/logger.c,duck/kernel/page.c,duck/kernel/memory.c,duck/kernel/vmemory.c'#-DUSE_POOL -finstrument-functions

add_files(
    'kernel.c',
    'thread.c',
    'schedule.c',
    'syscall.c',
    'exceptions.c',
    'memory.c',
    'module.c',
    'device.c',
    'vfs.c',
    'devfn.c',
    'fd.c',
    'loader.c',
    'mp.c',
    'trace.c',
    'logger.c',
    'vma.c',
    'page.c',
    'event.c'
)

add_includedirs(
    '../platform/{plat}',
    '../libs/include',
    '../',
    '../libs/include/archcommon'
)

arch=get_arch()
def_arch=arch.replace("-", "_").upper()

arch_type=get_arch_type()
def_arch_type=arch_type.replace( "-", "_").upper()

add_defines(def_arch)
add_defines(def_arch_type)

