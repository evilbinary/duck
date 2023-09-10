
target("algorithm")
    set_kind("static")
    set_arch(arch)
    
    add_files(
        'circle_queue.c',
        'pool.c',
        'queue_pool.c',
        'buffer.c'
     )

    add_includedirs(
        '../include/algorithm',
        '../include/kernel',
        '../include',
        '../../',
        {public = true}
    )
