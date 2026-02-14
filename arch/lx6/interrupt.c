/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "../interrupt.h"

#include "context.h"
#include "cpu.h"

#define INTERRUPT_SERVICE __attribute__((section(".iram0"),naked))


extern boot_info_t* boot_info;

interrupt_handler_t* interrutp_handlers[IDT_NUMBER];
extern u64 _idt[IDT_NUMBER];

void interrupt_init() {
  u64* pidt = _idt;

  boot_info->idt_base = pidt;

  boot_info->idt_number = IDT_NUMBER;

  // Register all interrupt handlers including window overflow/underflow
  interrupt_regist_all();

  // Set vecbase to _idt address where window overflow/underflow vectors are located
  // Note: Due to linker script ALIGN(0x1000), _idt is at 0x40081000, not 0x40080000
  cpu_set_vector((u32)_idt);
}

void interrupt_regist(u32 vec, interrupt_handler_t handler) {
  interrutp_handlers[vec] = handler;
  interrutp_set(vec);
}

void interrutp_set(int i) {
  u32 base = (u32)interrutp_handlers[i];
  u64* idt_base = boot_info->idt_base;
  //idt_base[i] = base;
}

INTERRUPT_SERVICE
void reset_handler() {
  interrupt_entering_code(0, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}

INTERRUPT_SERVICE
void l1_handler() {
  interrupt_entering_code(0, 0);
  interrupt_process(interrupt_default_handler);
  // cpu_halt();
  interrupt_exit();
}

INTERRUPT_SERVICE
void l2_handler() {
  interrupt_entering_code(0, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}

INTERRUPT_SERVICE
void l3_handler() {
  interrupt_entering_code(0, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}

INTERRUPT_SERVICE
void l4_handler() {
  interrupt_entering_code(0, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}

INTERRUPT_SERVICE
void l5_handler() {
  interrupt_entering_code(0, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}

INTERRUPT_SERVICE
void debug_excetpion_handler() {
  interrupt_entering_code(0, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}

INTERRUPT_SERVICE
void nmi_excetpion_handler() {
  interrupt_entering_code(0, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}

INTERRUPT_SERVICE
void kernel_excetpion_handler() {
  interrupt_entering_code(0, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

INTERRUPT_SERVICE
void user_excetpion_handler() {
  interrupt_entering_code(0, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}

INTERRUPT_SERVICE
void double_excetpion_handler() {
  interrupt_entering_code(0, 0);
  interrupt_process(interrupt_default_handler);
  // cpu_halt();
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

extern void window_overflow_4();
extern void window_underflow_4();
extern void window_overflow_8();
extern void window_underflow_8();
extern void window_overflow_12();
extern void window_underflow_12();

void interrupt_regist_all() {
  interrupt_regist(0, window_overflow_4);  // reset
  interrupt_regist(1, window_underflow_4);
  interrupt_regist(2, window_overflow_8);
  interrupt_regist(3, window_underflow_8);
  interrupt_regist(4, window_overflow_12);
  interrupt_regist(5, window_underflow_12);


  interrupt_regist(6, l2_handler);
  interrupt_regist(7, l3_handler);
  interrupt_regist(8, l4_handler);
  interrupt_regist(9, l5_handler);

  interrupt_regist(10, debug_excetpion_handler);
  interrupt_regist(11, nmi_excetpion_handler);
  interrupt_regist(12, kernel_excetpion_handler);
  interrupt_regist(13, user_excetpion_handler);
  interrupt_regist(16, double_excetpion_handler);
}
