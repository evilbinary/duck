ENTRY(_kstart)
K_VIR_ADDR = 0x42400000;
K_PHY_ADDR = 0x42400000;
PAGE_SIZE    = 0x100;
STACK_SIZE = 0x200;
SECTIONS
{
        /* Code. */
        __start = .;
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
        __bss_end = .;

        .stack :  ALIGN(4) {
                . = 0x6000;
                _estack = .;
                . = . + STACK_SIZE;
                _stack_abt = .;

                . = . + STACK_SIZE;
                _stack_undf = .;

                . = . + STACK_SIZE;
                _stack_irq = .;

                . = . + STACK_SIZE;
                _stack_svc = .;

                . = . + STACK_SIZE;
                _stack_sys = .;

                . = . + STACK_SIZE*4*4 ;
                _stack_other = .;

                
                . = ALIGN(4);
                _sstack = .;
        }
        
       __end = .;
}
