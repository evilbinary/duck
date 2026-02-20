ENTRY(_start)
K_VIR_ADDR = 0x100000;
K_PHY_ADDR = 0x100000;
PAGE_SIZE    = 0x100;
STACK_SIZE = 0x2000;
SECTIONS
{
        /* Code. */
        .text K_VIR_ADDR : AT(K_PHY_ADDR)
        {
		. = ALIGN(PAGE_SIZE);
                __start = .;
                KEEP(*(.text.boot))    /* Boot code must be first */
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
                _sdata = .;
                *(.data*)
                _edata = .;
        }
        /* Load address of .data section (in FLASH/ROM) */
        _sidata = LOADADDR(.data);
        __bss_start = .;
        /* Read-write data (uninitialized) and stack */
        .bss BLOCK(4K) : ALIGN(4K)
        {
                *(COMMON)
                *(.bss*)
        }
        __bss_end = .;

        . = ALIGN(16);
        _estack = . + STACK_SIZE;           /* Core 0 stack */
        _stack_abt = _estack + STACK_SIZE;
        _stack_undf = _stack_abt + STACK_SIZE;
        _stack_irq = _stack_undf + STACK_SIZE;
        _stack_svc = _stack_irq + STACK_SIZE;
        _stack_sys = _stack_svc + STACK_SIZE;
        _stack_other = _stack_sys;          /* AP stacks follow */
        _sstack = _stack_other + STACK_SIZE * 4;

        . = _sstack;                        /* Advance . past all stacks */
        __end = .;
}
