MEMORY {
  /*iram : org = 0x40082000, len = 0xFC00*/
  /* dram : org = 0x3ffc0000, len = 0x20000 */
  dram   (rw)   : ORIGIN = 0x3FFC0000, LENGTH = 0x20000  /* 0x3FFAE000-0x40000000 */
  iram   (rwx)  : ORIGIN = 0x40084000, LENGTH = 0x20000  /* 0x40080000-0x400A0000  */
  irom   (rx)   : ORIGIN = 0x400D0000, LENGTH = 0x20000 /* 0x400D0000-0x40400000  */
  drom   (r)    : ORIGIN = 0x3F400000, LENGTH = 0x20000 /* 0x3F400000-0x3F800000 */
}

ENTRY(_kstart)

SECTIONS {

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
    KEEP(*(.irq*))  
  } > iram


  .iram0 : ALIGN(4) {
    KEEP(*(.text.boot .text.boot.*)) 
    *(.literal*)
    *(.text .text.*)

    *(.iram0.literal*)
    KEEP(*(.kernel_excetpion_handler*))
  } > iram

  .text : ALIGN(4) {
 
  } > irom



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

PROVIDE ( UART0 = 0x3ff40000 );
PROVIDE ( SPI1 = 0x3ff42000 );
PROVIDE ( SPI0 = 0x3ff43000 );
PROVIDE ( GPIO = 0x3ff44000 );
PROVIDE ( SDM = 0x3ff44f00 );
PROVIDE ( RTCCNTL = 0x3ff48000 );
PROVIDE ( RTCIO = 0x3ff48400 );
PROVIDE ( SENS = 0x3ff48800 );
PROVIDE ( HINF = 0x3ff4B000 );
PROVIDE ( UHCI1 = 0x3ff4C000 );
PROVIDE ( I2S0 = 0x3ff4F000 );
PROVIDE ( UART1 = 0x3ff50000 );
PROVIDE ( I2C0 = 0x3ff53000 );
PROVIDE ( UHCI0 = 0x3ff54000 );
PROVIDE ( HOST = 0x3ff55000 );
PROVIDE ( RMT = 0x3ff56000 );
PROVIDE ( RMTMEM = 0x3ff56800 );
PROVIDE ( PCNT = 0x3ff57000 );
PROVIDE ( SLC = 0x3ff58000 );
PROVIDE ( LEDC = 0x3ff59000 );
PROVIDE ( EFUSE = 0x3ff5A000 );
PROVIDE ( MCPWM0 = 0x3ff5E000 );
PROVIDE ( TIMERG0 = 0x3ff5F000 );
PROVIDE ( TIMERG1 = 0x3ff60000 );
PROVIDE ( SPI2 = 0x3ff64000 );
PROVIDE ( SPI3 = 0x3ff65000 );
PROVIDE ( SYSCON = 0x3ff66000 );
PROVIDE ( I2C1 = 0x3ff67000 );
PROVIDE ( SDMMC = 0x3ff68000 );
PROVIDE ( EMAC_DMA = 0x3ff69000 );
PROVIDE ( EMAC_EXT = 0x3ff69800 );
PROVIDE ( EMAC_MAC = 0x3ff6A000 );
PROVIDE ( TWAI = 0x3ff6B000 );
PROVIDE ( MCPWM1 = 0x3ff6C000 );
PROVIDE ( I2S1 = 0x3ff6D000 );
PROVIDE ( UART2 = 0x3ff6E000 );

