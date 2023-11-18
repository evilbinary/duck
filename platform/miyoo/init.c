#include "arch/io.h"

void uart_write(char a) {}

char uart_read() { return 0; }


void uart_send(unsigned int c) {

}

void platform_init() { io_add_write_channel(uart_write); }

void platform_end() {}

void platform_map(){
  
}

void ipi_enable(int cpu) {

}


void timer_init(int hz) {}

void timer_end() {}


void lcpu_send_start(u32 cpu, u32 entry) {
    
}