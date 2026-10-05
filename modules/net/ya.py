# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("mod-net")
set_kind("static")

add_deps(
    'kernel',
)

arch=get_arch()
arch_type=get_arch_type()
plat=get_plat()

# Platform-specific network drivers
plat_source={
    'raspi2':[
        'bcm2837.c',
    ],
    'raspi3':[
        'bcm2837.c',
    ],
    'dmulator':[
        'e1000.c',
    ],
    'qemu':[
        'e1000.c',
    ],
    'versatilepb':[
        'e1000.c',
    ],
    'v3s':[
        'v3s.c',
    ],
    'raspi5':[
        'rp1_gem.c',
    ],
}

# Architecture-specific drivers
arch_source={
    'x86': [
        'e1000.c',
    ],
    'arm':[
        # Add ARM-specific network drivers here
    ],
    'arm64':[
        # Add ARM64-specific network drivers here
    ]
}

# Common source files (always compiled)
common_source=[
    'net.c',
]

# lwIP 2.2.1（BSD）。只在真有网卡的平台编进来，避免 raspi2/3 的骨架驱动
# 把协议栈链进启动路径。
lwip_source=[
    'lwip/src/core/altcp.c',
    'lwip/src/core/altcp_alloc.c',
    'lwip/src/core/altcp_tcp.c',
    'lwip/src/core/def.c',
    'lwip/src/core/dns.c',
    'lwip/src/core/inet_chksum.c',
    'lwip/src/core/init.c',
    'lwip/src/core/ip.c',
    'lwip/src/core/mem.c',
    'lwip/src/core/memp.c',
    'lwip/src/core/netif.c',
    'lwip/src/core/pbuf.c',
    'lwip/src/core/raw.c',
    'lwip/src/core/stats.c',
    'lwip/src/core/sys.c',
    'lwip/src/core/tcp.c',
    'lwip/src/core/tcp_in.c',
    'lwip/src/core/tcp_out.c',
    'lwip/src/core/timeouts.c',
    'lwip/src/core/udp.c',
    'lwip/src/core/ipv4/etharp.c',
    'lwip/src/core/ipv4/icmp.c',
    'lwip/src/core/ipv4/ip4.c',
    'lwip/src/core/ipv4/ip4_addr.c',
    'lwip/src/core/ipv4/ip4_frag.c',
    'lwip/src/api/api_lib.c',
    'lwip/src/api/api_msg.c',
    'lwip/src/api/err.c',
    'lwip/src/api/if_api.c',
    'lwip/src/api/netbuf.c',
    'lwip/src/api/netdb.c',
    'lwip/src/api/netifapi.c',
    'lwip/src/api/sockets.c',
    'lwip/src/api/tcpip.c',
    'lwip/src/netif/ethernet.c',
    'port/sys_arch.c',
    'port/ethernetif.c',
    'port/lwip_port.c',
    'port/libc_stub.c',
]

# Build source list
source=[]

# Add architecture-specific sources
if arch_source.get(arch_type):
    source+=arch_source.get(arch_type)

# Add platform-specific sources (overrides arch-specific)
if plat_source.get(plat):
    source+=plat_source.get(plat)

if plat in ('raspi5', 'v3s'):
    source+=lwip_source

add_files(
    source+common_source
)

add_includedirs(
    'port',
    '.',
    '../',
    'lwip/src/include',
)

# 必须排在 libs/include/kernel 前面，否则 <string.h>/<stdio.h> 会撞上内核头。
add_cflags('-I' + os.scriptdir() + '/port', force = true)
