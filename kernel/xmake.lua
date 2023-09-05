target("kernel")
    set_kind("static")

    add_deps("archcommon")

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
        '../platform/$(plat)',
        '../libs/include',
        '../',
        '../libs/include/archcommon'

    )
    add_defines(def_arch)
    add_defines(def_arch_type)
