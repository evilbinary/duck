
target("archcommon")
    set_kind("static")
    set_arch(arch)
    
    add_files(
                "./"..arch_type.."/*.c"
                )

    add_includedirs(
        '../include',
        '../include/archcommon'
    )
