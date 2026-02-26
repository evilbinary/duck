/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "../interrupt.h"

#include "context.h"
#include "cpu.h"
#include "gpio.h"

extern boot_info_t* boot_info;
extern int _estack;

// Forward declaration: used as default vector target.
INTERRUPT_SERVICE void irq_handler();
static void* irq_dispatch(interrupt_context_t* ic);
static void* systick_dispatch(interrupt_context_t* ic);

static inline u32 read_ipsr(void) {
  u32 v;
  asm volatile("mrs %0, ipsr" : "=r"(v));
  return v;
}

interrupt_handler_t* interrutp_handlers[IDT_NUMBER];
// Cortex-M VTOR requires alignment (at least 128 bytes; 256 is safe here).
u32 idt[IDT_NUMBER] __attribute__((aligned(256)));

void interrupt_init() {
  interrupt_regist_all();

  kprintf("interrupt init\n");
  boot_info->idt_base = idt;
  boot_info->idt_number = IDT_NUMBER;
  for (int i = 0; i < boot_info->idt_number; i++) {
    interrutp_set(i);
  }

  u32 val = (u32)idt;

  SCB->VTOR = val;
  dsb();
  isb();

  // Debug: confirm VTOR really points at our table
  kprintf("vtor set:%x read:%x idt:%x idt0:%x idt15:%x idt16:%x\n",
          val, (u32)SCB->VTOR, (u32)idt, idt[0], idt[15], idt[16]);

#if defined(STM32F4XX)
  // Early boot: disable & clear all external IRQs.
  // Drivers should re-enable what they need after proper init/ack logic.
  for (int n = 0; n < 8; n++) {
    NVIC->ICER[n] = 0xFFFFFFFF;
    NVIC->ICPR[n] = 0xFFFFFFFF;
  }
#endif
}

void interrupt_regist(u32 vec, interrupt_handler_t handler) {
  interrutp_handlers[vec] = handler;
  interrutp_set(vec);
}

void interrutp_set(int i) {
  // Vector 0 is initial MSP value (not a function pointer).
  if (i == 0) {
    idt[0] = (u32)&_estack;
    return;
  }

  interrupt_handler_t h = interrutp_handlers[i];
  if (h == NULL) {
    h = (interrupt_handler_t)irq_handler;  // default handler
  }
  idt[i] = (u32)h;
}

INTERRUPT_SERVICE
void reset_handler() {
  interrupt_entering_code(EX_RESET, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}

INTERRUPT_SERVICE
void svc_handler() {
  interrupt_entering_code(EX_SYS_CALL, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit_ret();
}

INTERRUPT_SERVICE
void irq_handler() {
  interrupt_entering_code(EX_IRQ, 0);
  interrupt_process(irq_dispatch);
  interrupt_exit();
}

static void* irq_dispatch(interrupt_context_t* ic) {
  // If some peripheral IRQ is pending very early, it can storm because no driver
  // has acknowledged it yet. Identify the active IRQ via IPSR and disable it.
  static int printed = 0;
  u32 ipsr = read_ipsr();
  if (ipsr >= 16) {
    u32 irqn = ipsr - 16;
#if defined(STM32F4XX)
    if (!printed) {
      printed = 1;
      kprintf("early irq storm: disable irqn=%d ipsr=%x\n", irqn, ipsr);
    }
    NVIC_DisableIRQ((IRQn_Type)irqn);
    NVIC_ClearPendingIRQ((IRQn_Type)irqn);
#endif
  }
  return interrupt_default_handler(ic);
}

INTERRUPT_SERVICE
void sys_tick_handler() {
  interrupt_entering_code(EX_TIMER, 0);
  interrupt_process(systick_dispatch);
  interrupt_exit_ret();
}

static void* systick_dispatch(interrupt_context_t* ic) {
#if defined(STM32F4XX)
  // Keep HAL time base running for HAL_* timeouts/delays.
  HAL_IncTick();
#endif
  return interrupt_default_handler(ic);
}

INTERRUPT_SERVICE
void sys_pendsv_handler() {
  interrupt_entering_code(EX_SYS_CALL, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit_ret();
}


INTERRUPT_SERVICE
void hard_fault_handler() {
  interrupt_entering_code(EX_UNDEF, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

INTERRUPT_SERVICE
void bus_handler() {
  interrupt_entering_code(EX_DATA_FAULT, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

INTERRUPT_SERVICE
void usage_fault_handler() {
  interrupt_entering_code(EX_UNDEF, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

void exception_info(interrupt_context_t* ic) {
  static const char* exception_msg[] = {
      "NONE", "RESET", "NONE", "NONE", "NONE", "NONE", "NONE",       "NONE",
      "NONE", "NONE",  "NONE", "SVC",  "NONE", "NONE", "SYS PENDSV", "SYS TICK",
  };
  int cpu = cpu_get_id();
  if (ic->no < sizeof exception_msg) {
    kprintf("exception cpu %d no %d: %s\n----------------------------\n", cpu,
            ic->no, exception_msg[ic->no]);
  } else {
    kprintf("exception cpu %d no %d:\n----------------------------\n", cpu,
            ic->no);
  }
}

void interrupt_regist_all() {
  interrupt_regist(1, reset_handler);  // reset
  interrupt_regist(2, irq_handler);
  interrupt_regist(3, hard_fault_handler);

  interrupt_regist(4, bus_handler);
  interrupt_regist(5, bus_handler);  //
  interrupt_regist(6, usage_fault_handler);

  interrupt_regist(11, svc_handler);         // svc_handler
  interrupt_regist(14, sys_pendsv_handler);  // sys_pendsv_handler
  interrupt_regist(15, sys_tick_handler);    // sys_tick_handler
}