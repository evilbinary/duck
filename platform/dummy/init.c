#include "arch/arch.h"
#include "gpio.h"

void uart_send(char a) {}

u32 uart_receive() { return 0; }

void platform_init() { io_add_write_channel(uart_send); }

void platform_map() {}

void platform_end() {}

void timer_init(int hz) {}

void timer_end() {}

void ipi_enable(int cpu) {}

void lcpu_send_start(u32 cpu, u32 entry) {}