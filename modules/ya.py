# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("modules")
set_kind("static")
add_deps(
    'arch',
    'algorithm',
    'kernel'
)

platform_source = {
    'v3s': [
        'gpu/v3s.c',
        'mmc/v3s.c',
        # 'mmc/v3s-sd.c',
        'serial/v3s.c',
        'mouse/v3s.c',
        'i2c/i2c.c',
        'i2c/v3s.c',
        'gpio/gpio.c',
        'gpio/v3s.c',
        'spi/spi.c',
        'spi/v3s.c',
        'rtc/v3s.c',
    ],
    'cubieboard2': [
        'gpu/v3s.c',
        'mmc/v3s.c',
        'serial/v3s.c',
        'mouse/v3s.c',
        'i2c/i2c.c',
        'i2c/v3s.c',
        'gpio/gpio.c',
        'gpio/v3s.c',
        'spi/spi.c',
        'spi/v3s.c',
    ],
    'orangepi-pc': [
        'gpu/v3s.c',
        'mmc/v3s.c',
        'serial/v3s.c',
        'mouse/v3s.c',
        'i2c/i2c.c',
        'i2c/v3s.c',
        'gpio/gpio.c',
        'gpio/v3s.c',
        'spi/spi.c',
        'spi/v3s.c',
        'rtc/v3s.c',
    ],
    'raspi2':[
        'gpu/bcm2836.c',
        'gpu/mailbox.c',
        'mmc/bcm2836.c',
        'mmc/bcm2835_vc.c',
        'serial/bcm2836.c',
        'mouse/bcm2836.c',
        'i2c/i2c.c',
        'i2c/bcm2836.c',
        'gpio/gpio.c',
        'gpio/bcm2836.c',
        'spi/spi.c',
        'spi/bcm2836.c',
        'rtc/bcm2836.c',
    ],
    'stm32f4xx': [
        'gpio/gpio.c',
        'gpio/stm32.c',
        'i2c/i2c.c',
        'i2c/stm32.c',
        'spi/spi.c',
        'spi/stm32.c',
        'lcd/lcd.c',
        'lcd/st7735.c',
        'serial/stm32.c',
    ],
    'rk3128': [
        'gpu/rk3128.c',
        'mmc/rk3128.c',
        'serial/rk3128.c',
        'mouse/rk3128.c',
        'i2c/i2c.c',
        'i2c/rk3128.c',
        'gpio/gpio.c',
        'gpio/rk3128.c',
        'spi/spi.c',
        'spi/rk3128.c',
    ],
    'rk3288': [
        'gpu/rk3128.c',
        'mmc/rk3128.c',
        'serial/rk3128.c',
        'mouse/rk3128.c',
        'i2c/i2c.c',
        'i2c/rk3128.c',
        'gpio/gpio.c',
        'gpio/rk3128.c',
        'spi/spi.c',
        'spi/rk3128.c',
    ],
    'riscv-virt': [
        'serial/riscv-virt.c',
    ]
}

other=[]
module=[]


plat=get_plat()
arch_type=get_arch_type()



if platform_source[plat]:
    other = platform_source[plat]

if arch_type == 'arm' :
    module = [
        'fat/fat.c',
        'fat/byteordering.c',
        'fat/partition.c',
        'fat/mod.c',
        # 'fat32/fat32.c',

        'gpu/gpu.c',
        'mmc/sdhci.c',
        'hello/hello.c',
        'test/test.c',
        'test/test-fat.c',
    ]

elif arch_type == 'x86' : 
    module = [
            'pci.c',
            'pic/pic.c',
            'ahci/ahci.c',
            'dma/dma.c',

            'hello/hello.c',
            'serial/serial.c',
            'rtc/rtc.c',
            'vga/vga.c',
            'vga/mode.c',
            'vga/qemu.c',
            'mouse/mouse.c',
            'keyboard/keyboard.c',
            'fat32/fat32.c',

            'fat/fat.c',
            'fat/byteordering.c',
            'fat/partition.c',
            'fat/mod.c',

            'sound/sb16.c',
            'net/net.c',
            'net/e1000.c',

            'test/test.c',
            'test/test-ahci.c',
            'test/test-fat.c',
    ]

elif arch_type == 'xtensa' : 
    module = [
        'hello/hello.c',
    ]
elif arch_type == 'general' : 
    module = [
        'hello/hello.c',
        'serial/general.c',
        'mmc/sdhci.c',
        'mmc/general.c',

        'fat/fat.c',
        'fat/byteordering.c',
        'fat/partition.c',
        'fat/mod.c',

    ]
elif arch_type == 'riscv' : 
    module = [
        'hello/hello.c',

        # # 'fat/fat.c',
        # # 'fat/byteordering.c',
        # # 'fat/partition.c',
        # # 'fat/mod.c',
    ]
else:
    module = [
        'hello/hello.c',
    ]


module+=other
module+= ['dev/*.c']


if module :
    for m in get_config('modules'):
        module+=[m+'/*.c']
    

source = module


add_files(
    source
)

add_includedirs(
    '.',
    '../',
    '../libs/include',
    '../platform',
    '../kernel',
    '../../',
    '../platform/{plat}'
)

arch=get_arch()
def_arch=arch.replace("-", "_").upper()

arch_type=get_arch_type()
def_arch_type=arch_type.replace( "-", "_").upper()

add_defines(def_arch)
add_defines(def_arch_type)
