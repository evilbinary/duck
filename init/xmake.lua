target("kernel.elf")
    set_kind("binary")
    add_deps(
        'modules',
        'kernel',
        'arch',
        'platform',
        'archcommon',
        'kernelcommon',
        'algorithm'
    )

    add_files(
        'main.c',
        'module.c',
        'init.c',
        'monitor.c',
        'test.c'
    )

    if get_config('SINGLE_KERNEL') then 
        add_files(
                '../../boot/'+arch+'/boot.o',
                '../../boot/'+arch+'/init.o')
    end
    add_includedirs(
        '../platform/$(plat)',
        '../libs/include',
        '../',
        '../libs/include/archcommon'

    )
    add_defines(def_arch)
    add_defines(def_arch_type)

    add_ldflags("-T "..path.join(os.scriptdir(), "/../xlinker/link-$(plat).ld"),  {force = true})

    add_rules("kernel-objcopy")


target('boot-config')
    add_deps('kernel.elf')

    add_files(
        "$(buildir)/$(plat)/$(arch)/$(mode)/kernel.elf"
    )
    add_rules("kernel-gen")
