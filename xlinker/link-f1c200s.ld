ENTRY(_kstart)
K_VIR_ADDR = 0x82000000;
K_PHY_ADDR = 0x82000000;
PAGE_SIZE    = 0x100;
STACK_SIZE = 0x200;
SECTIONS
{
        /* Code. */
        .text K_VIR_ADDR : AT(K_PHY_ADDR)
        {
		. = ALIGN(PAGE_SIZE);
                __start = .;
                KEEP(*(.text.boot .text.boot.*)) 
                *(.text .text.*)
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
                __bss_start = .;
                *(COMMON)
                *(.bss .bss*)
                __bss_end = .;
        }
        
        .stack :  ALIGN(4K) {
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
       _end = .;
}
