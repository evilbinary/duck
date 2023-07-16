/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "loader.h"

#include "fd.h"
#include "kernel.h"
#include "kernel/elf.h"
#include "thread.h"
#include "vfs.h"

// #define LOAD_ELF_DEBUG 1

#ifdef LOAD_ELF_DEBUG
#define log_debug kprintf
#else
#define log_debug 
#endif
#define log_error kprintf

int load_elf(Elf32_Ehdr* elf_header, u32 fd) {
#ifdef LOAD_ELF_DEBUG
  int p = syscall0(SYS_GETPID);
  log_debug("load elf fd %d tid %d\n", fd,p);
#endif
  u32 offset = elf_header->e_phoff;
  if (elf_header->e_phnum > MAX_PHDR) {
    log_error("phnum %d > MAX_PHDR\n", elf_header->e_phnum);
    return -1;
  }
  Elf32_Phdr phdr[MAX_PHDR];
  kmemset(phdr, 0, MAX_PHDR * sizeof(Elf32_Phdr));
  syscall3(SYS_SEEK, fd, offset, 0);
  u32 nbytes =
      syscall3(SYS_READ, fd, phdr, sizeof(Elf32_Phdr) * elf_header->e_phnum);
  // kprintf("addr %x elf=%x\n\r", phdr, elf);
  u32 entry = elf_header->e_entry;
  u32 entry_txt = 0;
  for (int i = 0; i < elf_header->e_phnum; i++) {
#ifdef LOAD_ELF_DEBUG
    log_debug("ptype:%d addr:%x\n", phdr[i].p_type, phdr[i].p_paddr);
#endif

    switch (phdr[i].p_type) {
      case PT_NULL:
        log_debug(" %s %x %x %x %s %x %x \r\n", "NULL", phdr[i].p_offset,
                  phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                  phdr[i].p_memsz);
        break;
      case PT_LOAD: {
        if ((phdr[i].p_flags & PF_X) == PF_X) {
#ifdef LOAD_ELF_DEBUG
          log_debug(" %s %x %x %x %s %x %x \r\n", "LOAD X", phdr[i].p_offset,
                    phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                    phdr[i].p_memsz);
#endif

          char* start = phdr[i].p_offset;
          char* vaddr = phdr[i].p_vaddr;
          syscall3(SYS_SEEK, fd, start, 0);
          entry_txt = vaddr;
          u32 ret = syscall3(SYS_READ, fd, vaddr, phdr[i].p_filesz);
        } else {
#ifdef LOAD_ELF_DEBUG
          log_debug(" %s %x %x %x %s %x %x \r\n", "LOAD RW", phdr[i].p_offset,
                    phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                    phdr[i].p_memsz);
#endif
          char* start = phdr[i].p_offset;
          char* vaddr = phdr[i].p_vaddr;
          syscall3(SYS_SEEK, fd, start, 0);
          u32 ret = syscall3(SYS_READ, fd, vaddr, phdr[i].p_filesz);
        }
      } break;
      case PT_DYNAMIC:
        log_debug(" %s %x %x %x\r\n %s %x %x ", "DYNAMIC", phdr[i].p_offset,
                  phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                  phdr[i].p_memsz);
        break;
      case PT_INTERP:
        log_debug(" %s %x %x %x\r\n %s %x %x ", "INTERP", phdr[i].p_offset,
                  phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                  phdr[i].p_memsz);
        break;
      case PT_NOTE:
        log_debug(" %s %x %x %x\r\n %s %x %x ", "NOTE", phdr[i].p_offset,
                  phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                  phdr[i].p_memsz);
        break;
      case PT_SHLIB:
        log_debug(" %s %x %x %x\r\n %s %x %x ", "SHLIB", phdr[i].p_offset,
                  phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                  phdr[i].p_memsz);
        break;
      case PT_PHDR:
        log_debug(" %s %x %x %x\r\n %s %x %x ", "PHDR", phdr[i].p_offset,
                  phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                  phdr[i].p_memsz);
        break;
      case PT_TLS:
        log_debug(" %s %x %x %x\r\n %s %x %x ", "TLS", phdr[i].p_offset,
                  phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                  phdr[i].p_memsz);
        break;
      case PT_NUM:
        kprintf(" %s %x %x %x\r\n %s %x %x ", "NUM", phdr[i].p_offset,
                phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                phdr[i].p_memsz);
        break;
      case PT_GNU_EH_FRAME:
        log_debug(" %s %x %x %x\r\n %s %x %x ", "GNU_EH_FRAME",
                  phdr[i].p_offset, phdr[i].p_vaddr, phdr[i].p_paddr, "",
                  phdr[i].p_filesz, phdr[i].p_memsz);
        break;
      case PT_GNU_RELRO:
        log_debug(" %s %x %x %x\r\n %s %x %x ", "GNU_RELRO", phdr[i].p_offset,
                  phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                  phdr[i].p_memsz);
        break;
      case PT_GNU_STACK:
#ifdef LOAD_ELF_DEBUG
        log_debug(" %s %x %x %x\r\n %s %x %x ", "GNU_STACK", phdr[i].p_offset,
                  phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                  phdr[i].p_memsz);
#endif
        break;
      default:
        break;
    }
  }
  return 0;
}

void run_elf_thread(void* a) {
  log_debug("run load elf\n");
  Elf32_Ehdr elf;
  thread_t thread;
  thread_t* current = &thread;
  u32 ret = syscall1(SYS_SELF, current);
  if (ret < 0) {
    log_error("get current thread error\n");
    return;
  }
    log_debug("run load elf1\n");

  exec_t* exec = current->exec;
  if (exec == NULL) {
    log_error("get current thread exec error\n");
    return;
  }
      log_debug("run load elf2\n");

  u32 fd = syscall2(SYS_OPEN, exec->filename, 0);
#ifdef LOAD_ELF_NAME_DEBUG
  log_debug("load elf %s fd:%d tid:%d\n", exec->filename, fd, current->id);
#endif
  if (fd < 0) {
    log_error("open elf %s error\n", exec->filename);
    syscall1(SYS_EXIT, -1);
    return;
  }
      log_debug("run load elf1\n");

  u32 nbytes = syscall3(SYS_READ, fd, &elf, sizeof(Elf32_Ehdr));

  Elf32_Ehdr* elf_header = (Elf32_Ehdr*)&elf;
  entry_fn entry = NULL;
  if (elf_header->e_ident[0] == ELFMAG0 || elf_header->e_ident[1] == ELFMAG1) {
    entry = elf_header->e_entry;
    load_elf(elf_header, fd);
  } else {
    log_error("load faild not elf %s\n", exec->filename);
    entry = NULL;
    syscall1(SYS_EXIT, -1);
  }
  ret = -1;
  if (entry != NULL) {
#ifdef LOAD_ELF_DEBUG
    log_debug("entry %x\n", entry);
#endif
    exec_t exec_info = *exec;
    ret = entry(&exec_info);
  }else{
    log_error("entry not found\n");
    syscall1(SYS_EXIT, -1);
  }
}
