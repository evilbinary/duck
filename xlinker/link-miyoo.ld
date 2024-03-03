ENTRY(_kstart)
K_VIR_ADDR = 0x20008000;
K_PHY_ADDR = 0x20008000;
PAGE_SIZE    = 0x100;
STACK_SIZE = 0x200;
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
        __bss_start = .;
        /* Read-write data (uninitialized) and stack */
        .bss BLOCK(4K) : ALIGN(4K)
        {
                *(COMMON)
                *(.bss*)
        }
        __bss_end = .;
        
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
}
