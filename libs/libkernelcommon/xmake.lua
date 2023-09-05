
target("kernelcommon")
    set_kind("static")
    set_arch(arch)
    
    add_files(
    'string.c',
    'vsprintf.c',
    'common.c',
    'io.c',
    'stack_guard.c'
    )

    add_includedirs(
    '../include/algorithm',
    '../include/kernel',
    '../include',
    '../../'
    )
