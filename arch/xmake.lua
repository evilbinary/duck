target("arch")
    set_kind("static")

    add_deps("archcommon")

    add_files(
    'arch.c',
    'lock.c',
    'pmemory.c',
    'interrupt.c'
    )
    
    add_files(
            "./$(arch)/*.c",
            "./$(arch)/*.s")
    add_includedirs(
        '../platform/$(plat)',
        '../libs/include',
        '../',
        '../libs/include/archcommon'

    )
    add_defines(def_arch)
    add_defines(def_arch_type)
