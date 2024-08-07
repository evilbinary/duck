/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "loader.h"
#include "posix/sysfn.h"

// #define LOAD_ELF_DEBUG 1

#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4

#ifdef LOAD_ELF_DEBUG
#define log_debug kprintf
#else
#define log_debug
#endif
#define log_error kprintf

static void print_hex(u8 *addr, u32 size) {
  for (int x = 0; x < size; x++) {
    kprintf("%02x ", addr[x]);
    if (x != 0 && (x % 32) == 0) {
      kprintf("\n");
    }
  }
  kprintf("\n\r");
}

int load_elf(Elf32_Ehdr* elf_header, u32 fd, void* arg, u32 base) {
#ifdef LOAD_ELF_DEBUG
  int p = syscall0(SYS_GETPID);
  log_debug("load elf fd %d tid %d\n", fd, p);
#endif
  u32 offset = elf_header->e_phoff;
  if (elf_header->e_phnum > MAX_PHDR) {
    log_error("phnum %d > MAX_PHDR\n", elf_header->e_phnum);
    return -1;
  }
  Elf32_Phdr phdr[MAX_PHDR];
  kmemset(phdr, 0, MAX_PHDR * sizeof(Elf32_Phdr));
  char* interp_buf[MAX_INTERP_PATH];
  char* interp_name = NULL;

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
          char* vaddr = phdr[i].p_vaddr + base;
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
          char* vaddr = phdr[i].p_vaddr + base;
          syscall3(SYS_SEEK, fd, start, 0);
          u32 ret = syscall3(SYS_READ, fd, vaddr, phdr[i].p_filesz);

          log_debug("content =%x\n", *((int*)vaddr));
        }
      } break;
      case PT_ARM_EXIDX: {
        log_debug(" %s %x %x %x %s %x %x ", "EXIDX", phdr[i].p_offset,
                  phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                  phdr[i].p_memsz);
        char* start = phdr[i].p_offset;
        char* vaddr = phdr[i].p_vaddr + base;
        //syscall3(SYS_SEEK, fd, start, 0);
        //u32 ret = syscall3(SYS_READ, fd, vaddr, phdr[i].p_filesz);

      } break;
      case PT_DYNAMIC:
        log_debug(" %s %x %x %x %s %x %x \r\n", "DYNAMIC", phdr[i].p_offset,
                  phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                  phdr[i].p_memsz);
        break;
      case PT_INTERP:
        log_debug(" %s %x %x %x %s %x %x \r\n", "INTERP", phdr[i].p_offset,
                  phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                  phdr[i].p_memsz);
        char* start = phdr[i].p_offset;
        syscall3(SYS_SEEK, fd, start, 0);
        u32 ret = syscall3(SYS_READ, fd, interp_buf, phdr[i].p_filesz);
        interp_name = interp_buf;
        break;
      case PT_NOTE:
        log_debug(" %s %x %x %x %s %x %x \r\n", "NOTE", phdr[i].p_offset,
                  phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                  phdr[i].p_memsz);
        break;
      case PT_SHLIB:
        log_debug(" %s %x %x %x %s %x %x \r\n", "SHLIB", phdr[i].p_offset,
                  phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                  phdr[i].p_memsz);
        break;
      case PT_PHDR:
        log_debug(" %s %x %x %x %s %x %x \r\n", "PHDR", phdr[i].p_offset,
                  phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                  phdr[i].p_memsz);
        break;
      case PT_TLS:
        log_debug(" %s %x %x %x %s %x %x ", "TLS", phdr[i].p_offset,
                  phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                  phdr[i].p_memsz);
        break;
      case PT_NUM:
        kprintf(" %s %x %x %x %s %x %x \r\n", "NUM", phdr[i].p_offset,
                phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                phdr[i].p_memsz);
        break;
      case PT_GNU_EH_FRAME:
        log_debug(" %s %x %x %x %s %x %x \r\n", "GNU_EH_FRAME",
                  phdr[i].p_offset, phdr[i].p_vaddr, phdr[i].p_paddr, "",
                  phdr[i].p_filesz, phdr[i].p_memsz);
        break;
      case PT_GNU_RELRO:
        log_debug(" %s %x %x %x %s %x %x \r\n", "GNU_RELRO", phdr[i].p_offset,
                  phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                  phdr[i].p_memsz);
        break;
      case PT_GNU_STACK:
#ifdef LOAD_ELF_DEBUG
        log_debug(" %s %x %x %x %s %x %x \r\n", "GNU_STACK", phdr[i].p_offset,
                  phdr[i].p_vaddr, phdr[i].p_paddr, "", phdr[i].p_filesz,
                  phdr[i].p_memsz);
#endif
        break;
      default:
        break;
    }
  }
  if (interp_name != NULL) {
    log_debug("interp name %s\n", interp_name);
    load_elf_interp(interp_name, arg);
  }

  return 0;
}

void auxv_set(Elf32_auxv_t* auxv, int a_type, int a_val) {
  auxv[a_type].a_type = a_type;
  auxv[a_type].a_un.a_val = a_val;
}

void build_env(char** env) {
  char** envp = env;
  Elf32_auxv_t* auxv = envp;

  auxv_set(auxv++, AT_IGNORE, 0);
  auxv_set(auxv++, AT_EXECFD, 0);
  auxv_set(auxv++, AT_PHDR, 0);
  auxv_set(auxv++, AT_PHENT, 0);
  auxv_set(auxv++, AT_PHNUM, 0);
  auxv_set(auxv++, AT_PAGESZ, PAGE_SIZE);
  auxv_set(auxv++, AT_BASE, 0);
  auxv_set(auxv++, AT_FLAGS, 0);
  auxv_set(auxv++, AT_ENTRY, 0);
  auxv_set(auxv++, AT_NOTELF, 0);
  auxv_set(auxv++, AT_UID, 0);
  auxv_set(auxv++, AT_EUID, 0);
  auxv_set(auxv++, AT_GID, 0);
  auxv_set(auxv++, AT_EGID, 0);
  auxv_set(auxv++, AT_PLATFORM, 0);
  auxv_set(auxv++, AT_HWCAP, 0);
  auxv_set(auxv++, AT_CLKTCK, 0);
  auxv_set(auxv++, AT_NULL, 0);
}

void run_elf_thread(long* p) {
  log_debug("run load elf\n");
  Elf32_Ehdr elf;
  int ret = 0;
  if (p == NULL) {
    log_error("get current thread args error\n");
    return;
  }

  int argc = p[0];
  char** argv = (void*)(p + 1);
  char** envp = argv + argc + 1;
  char* filename = p[1];

  while (*envp++ != NULL)
    ;
  build_env(envp);

  log_debug("envp==>%x\n", envp);

  log_debug("run load elf %s\n", filename);

  u32 fd = syscall2(SYS_OPEN, filename, 0);
#ifdef LOAD_ELF_NAME_DEBUG
  log_debug("load elf %s fd:%d tid:%d\n", filename, fd, current->id);
#endif
  if (fd < 0) {
    log_error("open elf %s error\n", filename);
    syscall1(SYS_EXIT, -1);
    return;
  }
  log_debug("run load elf1\n");

  u32 nbytes = syscall3(SYS_READ, fd, &elf, sizeof(Elf32_Ehdr));

  Elf32_Ehdr* elf_header = (Elf32_Ehdr*)&elf;
  entry_fn entry = NULL;
  if (elf_header->e_ident[0] == ELFMAG0 || elf_header->e_ident[1] == ELFMAG1) {
    entry = elf_header->e_entry;
    load_elf(elf_header, fd, p, 0);
  } else {
    log_error("load faild not elf %s\n", filename);
    entry = NULL;
    syscall1(SYS_EXIT, -1);
  }
  ret = -1;
  go_start(entry, p);
}

void go_start(entry_fn entry, long* args) {
  log_debug("args %x\n", args);
  int ret;
  if (entry != NULL) {
#ifdef LOAD_ELF_DEBUG
    log_debug("entry %x\n", entry);
    print_hex(entry,0x1000);
#endif
    ret = entry(args);
  } else {
    log_error("entry not found\n");
    for(;;){
      syscall1(SYS_EXIT, -1);
    }
  }
}

void* load_elf_interp(char* filename, void* args) {
  Elf32_Ehdr elf;
  int ret = 0;
  u32 fd = syscall2(SYS_OPEN, filename, 0);
  if (fd < 0) {
    log_error("open elf %s error\n", filename);
    return NULL;
  }
  u32 nbytes = syscall3(SYS_READ, fd, &elf, sizeof(Elf32_Ehdr));
  Elf32_Ehdr* elf_header = (Elf32_Ehdr*)&elf;
  void* entry = NULL;
  if (elf_header->e_ident[0] == ELFMAG0 || elf_header->e_ident[1] == ELFMAG1) {
    entry = elf_header->e_entry;
    void* base = (size_t)sys_mmap2(0, 100 * PAGE_SIZE, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANON, -1, 0);
    entry = (u32)entry + (u32)base;
    load_elf(elf_header, fd, args, base);
  } else {
    // log_error("load faild not elf %s\n", filename);
    entry = NULL;
  }
  log_debug("interp entry %x\n", entry);
  // go_start(entry, args);
  return entry;
}
