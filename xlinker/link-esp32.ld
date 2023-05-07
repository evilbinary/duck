MEMORY {
  /*iram : org = 0x40082000, len = 0xFC00*/
  /* dram : org = 0x3ffc0000, len = 0x20000 */
  dram   (rw)   : ORIGIN = 0x3FFC0000, LENGTH = 0x20000  /* 0x3FFAE000-0x40000000 */
  iram   (rwx)  : ORIGIN = 0x40084000, LENGTH = 0x20000  /* 0x40080000-0x400A0000  */

  drom   (r)    : ORIGIN = 0x3F400000, LENGTH = 0x20000 /* 0x3F400000-0x3F800000 */
  irom   (rx)   : ORIGIN = 0x400D0000, LENGTH = 0x20000 /* 0x400D0000-0x40400000  */
}

ENTRY(_kstart)

SECTIONS {
  .text : ALIGN(4) {
    *(.text.boot)
    *(.literal*)
    *(.text .text.*)
  } > irom

  .text.vectors : ALIGN(4) {
    . = ALIGN(64);
    _idt = .;
    KEEP(*(.WindowOverflow4.text));
    . = ALIGN(64);
    KEEP(*(.WindowUnderflow4.text));
    . = ALIGN(64);
    KEEP(*(.WindowOverflow8.text));
    . = ALIGN(64);
    KEEP(*(.WindowUnderflow8.text));
    . = ALIGN(64);
    KEEP(*(.WindowOverflow12.text));
    . = ALIGN(64);
    KEEP(*(.WindowUnderflow12.text));
    . = ALIGN(64);
    KEEP(*(.Level2InterruptVector.text));
    . = ALIGN(64);
    KEEP(*(.Level3InterruptVector.text));
    . = ALIGN(64);
    KEEP(*(.Level4InterruptVector.text));
    . = ALIGN(64);
    KEEP(*(.Level5InterruptVector.text));
    . = ALIGN(64);
    KEEP(*(.DebugExceptionVector.text));
    . = ALIGN(64);
    KEEP(*(.NMIExceptionVector.text));
    . = ALIGN(64);
    KEEP(*(.KernelExceptionVector.text));
    . = ALIGN(64);
    KEEP(*(.UserExceptionVector.text));
    . = ALIGN(128);
    KEEP(*(.DoubleExceptionVector.text));
    . = ALIGN(64);
    . = ALIGN(0x400);
    KEEP(*(.vector))
    KEEP(*(.vector*))
  } > iram

  .bss : ALIGN(4) {
     _bss_start = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
	_bss_end = .;

  } > dram
  
  .data : ALIGN(4) {
    _data_start = .;
    *(.data)
    *(.data*)
    *(.init_array*)
    *(.fini_array*)
    *(.preinit_array*)
  	_data_end = .;
  } > dram


  .rodata : ALIGN(4) {
    *(.rodata .rodata.*)
  } > drom
}

