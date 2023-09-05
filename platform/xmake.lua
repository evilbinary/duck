target("platform")
    set_kind("static")

    add_deps("archcommon")

    add_files(
        './$(plat)/*.c'
    )


    add_includedirs(
        '../platform/$(plat)',
        '../libs/include',
        '../',
        '../libs/include/'

    )
    add_defines(def_arch)
    add_defines(def_arch_type)
