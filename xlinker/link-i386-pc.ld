ENTRY(_kstart)
K_VIR_ADDR = 0x100000;
K_PHY_ADDR = 0x100000;
PAGE_SIZE    = 0x100;
SECTIONS
{
        /* Code. */
        .text K_VIR_ADDR : AT(K_PHY_ADDR)
        {
                __start = .;
		. = ALIGN(PAGE_SIZE);
                *(.text.boot)
                *(.text*)
        }

        /* Read-only data. */
        .rodata BLOCK(4K) : ALIGN(4K)
        {
                *(.rodata*)
        }

        /* Read-write data (initialized) */
        .data BLOCK(4K) : ALIGN(4K)
        {
                *(.data*)
        }

        /* Read-write data (uninitialized) and stack */
        .bss BLOCK(4K) : ALIGN(4K)
        {
                *(COMMON)
                *(.bss*)
        }
        .eh_frame BLOCK(4K) : ALIGN(4K)
        {
                *(.eh_frame)
        }
       _end = .;
}
