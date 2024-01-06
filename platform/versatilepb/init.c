#include "arch/io.h"
#include "arch/arch.h"

void uart_write(char a) {}

char uart_read() { return 0; }

void platform_init() { io_add_write_channel(uart_write); }

void platform_map() {}

void platform_end() {}

void timer_init(int hz) {}

void timer_end() {}

void ipi_enable(int cpu) {}


void lcpu_send_start(u32 cpu, u32 entry) {
    
}