
target("archcommon")
    set_kind("static")
    set_arch(arch)
    
    if arch_type then
        add_files(
                    "./"..arch_type.."/*.c"
                    )
    end

    add_includedirs(
        '../include',
        '../include/archcommon'
    )
