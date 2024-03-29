ENTRY(kstart)
K_VIR_ADDR = 0x82000000;
K_PHY_ADDR = 0x82000000;
PAGE_SIZE    = 0x100;
__heap_size  = 0x200;
__stack_size = 0x200;
SECTIONS
{
        . = K_VIR_ADDR;
        /* Code. */
        .text K_VIR_ADDR : AT(K_PHY_ADDR)
        {
		. = ALIGN(PAGE_SIZE);
                *(.text)
        }

        /* Read-only data. */
        .rodata BLOCK(4K) : ALIGN(4K)
        {
                *(.rodata)
        }

        /* Read-write data (initialized) */
        .data BLOCK(4K) : ALIGN(4K)
        {
                *(COMMON)
                *(.data)
        }

        /* Read-write data (uninitialized) and stack */
        __bss_start = .;
        .bss BLOCK(4K) : ALIGN(4K)
        {
                *(.bss)
        }
        . = . + __stack_size;
        _stack_irq = .;

        . = . + __stack_size;
        _stack_svc = .;
        __bss_end = .;
       __end = .;
}
