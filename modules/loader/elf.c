/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "kernel/elf.h"
#include "kernel/memory.h"
#include "kernel/thread.h"
#include "libs/include/kernel/string.h"
#include "loader.h"
#include "posix/sysfn.h"

#if defined(ARM) && !defined(ARM64) && !defined(__aarch64__)
extern void cp15_invalidate_icache(void);
#endif


#define LOAD_ELF_DEBUG 1

#ifdef LOAD_ELF_DEBUG
#define elf32_log_debug kprintf
#else
#define elf32_log_debug(...)
#endif

#define elf32_log_error kprintf

typedef struct exec_stack_layout {
  u32 sp;
  u32 stack_top;
  char** argv;
  char** envp;
  Elf32_auxv_t* auxv;
  char* execfn;
} exec_stack_layout_t;

static u32 elf32_align_down(u32 val, u32 align) { return val & ~(align - 1); }

static u32 elf32_align_up(u32 val, u32 align) {
  return (val + align - 1) & ~(align - 1);
}

static void elf32_user_cache_sync(void* user_addr, u32 size) {
  if (user_addr == NULL || size == 0) {
    return;
  }
#if defined(ARM64) || defined(__aarch64__)
  uintptr_t start = (uintptr_t)user_addr & ~63UL;
  uintptr_t end =
      ((uintptr_t)user_addr + size + 63UL) & ~63UL;
  for (uintptr_t va = start; va < end; va += 64) {
    asm volatile("dc civac, %0" : : "r"(va) : "memory");
  }
  asm volatile("dsb ish" ::: "memory");
  asm volatile("isb" ::: "memory");
#elif defined(ARM) || defined(ARMV7_A) || defined(ARMV7) || defined(ARMV5) || \
    defined(__arm__)
  u32 start = (u32)(uintptr_t)user_addr & ~31U;
  u32 end = ((u32)(uintptr_t)user_addr + size + 31U) & ~31U;
  for (u32 va = start; va < end; va += 32) {
    asm volatile("mcr p15, 0, %0, c7, c14, 1" : : "r"(va) : "memory");
  }
  asm volatile("dsb sy" ::: "memory");
  asm volatile("isb sy" ::: "memory");
#else
  (void)user_addr;
  (void)size;
#endif
}

static void* elf32_user_ptr(thread_t* current, void* user_addr, u32 size) {
#ifdef VM_ENABLE
  if (current != NULL && current->vm != NULL && user_addr != NULL && size > 0) {
    u32 start = (u32)user_addr & ~(PAGE_SIZE - 1);
    u32 end = elf32_align_up((u32)user_addr + size, PAGE_SIZE);
    for (u32 va = start; va < end; va += PAGE_SIZE) {
      if (page_v2p(current->vm->upage, (void*)va) == NULL) {
        if (valloc((void*)va, PAGE_SIZE) == NULL) {
          elf32_log_error("elf32 valloc user page failed va=%x\n", va);
          return NULL;
        }
      }
    }
  }
#endif
  return user_addr;
}

static void* elf32_user_access(thread_t* current, void* user_addr, u32 size) {
  if (user_addr == NULL || size == 0) {
    return NULL;
  }
  if (elf32_user_ptr(current, user_addr, size) == NULL) {
    return NULL;
  }
#ifdef VM_ENABLE
  void* mapped = kpage_v2p(user_addr, size);
  if (mapped != NULL) {
    return mapped;
  }
#endif
  return user_addr;
}

static void elf32_user_memset(thread_t* current, void* user_dst, int val,
                              u32 size) {
  if (size == 0 || user_dst == NULL) {
    return;
  }
  void* dst = elf32_user_access(current, user_dst, size);
  if (dst == NULL) {
    elf32_log_error("elf32 user memset map failed dst=%x size=%x\n", user_dst,
                    size);
    return;
  }
  kmemset(dst, val, size);
  elf32_user_cache_sync(user_dst, size);
}

static void elf32_user_memcpy(thread_t* current, void* user_dst, const void* src,
                              u32 size) {
  if (size == 0 || user_dst == NULL || src == NULL) {
    return;
  }
  void* dst = elf32_user_access(current, user_dst, size);
  if (dst == NULL) {
    elf32_log_error("elf32 user memcpy map failed dst=%x size=%x\n", user_dst,
                    size);
    return;
  }
  kmemcpy(dst, src, size);
  elf32_user_cache_sync(user_dst, size);
}

static int elf32_user_read(thread_t* current, void* user_src, void* dst,
                           u32 size) {
  if (size == 0 || user_src == NULL || dst == NULL) {
    return -1;
  }
  void* src = elf32_user_access(current, user_src, size);
  if (src == NULL) {
    return -1;
  }
  kmemcpy(dst, src, size);
  return 0;
}

static void elf32_auxv_put(u32* buf, u32* idx, u32 type, u32 val) {
  u32 i = *idx;
  buf[i++] = type;
  buf[i++] = val;
  *idx = i;
}

static int elf32_ptr_looks_like_ascii(u32 val) {
  if (val < 0x10000U) {
    return 0;
  }
  for (int shift = 0; shift < 32; shift += 8) {
    u8 b = (u8)(val >> shift);
    if (b == 0) {
      break;
    }
    if (b < 0x20 || b > 0x7e) {
      return 0;
    }
  }
  return 1;
}

static int elf32_reset_user_stack(thread_t* current, u32 bottom, u32 top) {
  if (current == NULL || top <= bottom) {
    return -1;
  }
  for (u32 va = bottom; va < top; va += PAGE_SIZE) {
    if (valloc((void*)va, PAGE_SIZE) == NULL) {
      elf32_log_error("elf32 reset stack page failed va=%x\n", va);
      return -1;
    }
  }
  elf32_user_memset(current, (void*)bottom, 0, top - bottom);
  return 0;
}

static int elf32_is_valid(const unsigned char* e_ident) {
  return e_ident[EI_MAG0] == ELFMAG0 && e_ident[EI_MAG1] == ELFMAG1 &&
         e_ident[EI_MAG2] == ELFMAG2 && e_ident[EI_MAG3] == ELFMAG3 &&
         e_ident[EI_CLASS] == ELFCLASS32 && e_ident[EI_DATA] == ELFDATA2LSB;
}

static int elf32_read_phdrs(int fd, const Elf32_Ehdr* ehdr, Elf32_Phdr* phdr) {
  sys_seek(fd, ehdr->e_phoff, 0);
  u32 need = sizeof(Elf32_Phdr) * ehdr->e_phnum;
  u32 got = sys_read(fd, phdr, need);
  if (got != need) {
    elf32_log_error("elf32 read phdr failed need=%d got=%d\n", need, got);
    return -1;
  }
  return 0;
}

static int elf32_scan_load_range(const Elf32_Ehdr* ehdr, const Elf32_Phdr* phdr,
                                 u32* min_vaddr, u32* max_vaddr) {
  int found = 0;
  *min_vaddr = ~0U;
  *max_vaddr = 0;
  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD || phdr[i].p_memsz == 0) {
      continue;
    }
    u32 start = elf32_align_down(phdr[i].p_vaddr, PAGE_SIZE);
    u32 end = elf32_align_up(phdr[i].p_vaddr + phdr[i].p_memsz, PAGE_SIZE);
    if (start < *min_vaddr) *min_vaddr = start;
    if (end > *max_vaddr) *max_vaddr = end;
    found = 1;
  }
  return found ? 0 : -1;
}

static u32 elf32_locate_phdr(const Elf32_Ehdr* ehdr, const Elf32_Phdr* phdr) {
  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_PHDR) {
      return phdr[i].p_vaddr;
    }
  }
  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD) {
      continue;
    }
    u32 start = phdr[i].p_offset;
    u32 end = phdr[i].p_offset + phdr[i].p_filesz;
    if (ehdr->e_phoff >= start &&
        (ehdr->e_phoff + ehdr->e_phnum * ehdr->e_phentsize) <= end) {
      return phdr[i].p_vaddr + (ehdr->e_phoff - phdr[i].p_offset);
    }
  }
  return 0;
}

static void elf32_capture_interp(int fd, const Elf32_Phdr* ph, char* out) {
  u32 len = ph->p_filesz;
  if (len >= MAX_INTERP_PATH) {
    len = MAX_INTERP_PATH - 1;
  }
  sys_seek(fd, ph->p_offset, 0);
  sys_read(fd, out, len);
  out[len] = '\0';
}

static int elf32_map_segment(int fd, const Elf32_Phdr* ph) {
  if (ph->p_memsz == 0) {
    return 0;
  }

  u32 seg_page = elf32_align_down(ph->p_vaddr, PAGE_SIZE);
  u32 seg_end = elf32_align_up(ph->p_vaddr + ph->p_memsz, PAGE_SIZE);
  u32 seg_size = seg_end - seg_page;

  if (valloc((void*)seg_page, seg_size) == NULL) {
    elf32_log_error("elf32 map segment valloc failed vaddr=%x size=%x\n",
                    seg_page, seg_size);
    return -1;
  }
  {
    thread_t* current = thread_current();
    elf32_user_memset(current, (void*)seg_page, 0, seg_size);
  }

  if (ph->p_filesz != 0) {
    sys_seek(fd, ph->p_offset, 0);
    u32 got = sys_read(fd, (void*)ph->p_vaddr, ph->p_filesz);
    if (got != ph->p_filesz) {
      elf32_log_error("elf32 read segment failed off=%x got=%x need=%x\n",
                      ph->p_offset, got, ph->p_filesz);
      return -1;
    }
    elf32_user_cache_sync((void*)ph->p_vaddr, ph->p_filesz);
  }
  return 0;
}

static int elf32_load_image_fd(int fd, const Elf32_Ehdr* ehdr,
                               elf32_image_info_t* image) {
  Elf32_Phdr phdr[MAX_PHDR];
  u32 min_vaddr = 0;
  u32 max_vaddr = 0;

  if (ehdr->e_phnum > MAX_PHDR || ehdr->e_phentsize != sizeof(Elf32_Phdr)) {
    elf32_log_error("bad elf32 phdr table phnum=%d entsize=%d\n", ehdr->e_phnum,
                    ehdr->e_phentsize);
    return -1;
  }
  if (ehdr->e_type != ET_EXEC) {
    elf32_log_error("elf32 unsupported type %d\n", ehdr->e_type);
    return -1;
  }

  kmemset(phdr, 0, sizeof(phdr));
  if (elf32_read_phdrs(fd, ehdr, phdr) < 0) {
    return -1;
  }
  if (elf32_scan_load_range(ehdr, phdr, &min_vaddr, &max_vaddr) < 0) {
    elf32_log_error("elf32 has no PT_LOAD\n");
    return -1;
  }

  kmemset(image, 0, sizeof(*image));
  image->base = 0;
  image->entry = ehdr->e_entry;
  image->phent = ehdr->e_phentsize;
  image->phnum = ehdr->e_phnum;
  image->phdr = elf32_locate_phdr(ehdr, phdr);
  kmemcpy(image->phdrs, phdr, sizeof(Elf32_Phdr) * ehdr->e_phnum);

  for (int i = 0; i < ehdr->e_phnum; i++) {
    switch (phdr[i].p_type) {
      case PT_LOAD:
        if (elf32_map_segment(fd, &phdr[i]) < 0) {
          return -1;
        }
        break;
      case PT_INTERP:
        elf32_capture_interp(fd, &phdr[i], image->interp_path);
        break;
      case PT_TLS:
        image->tls_vaddr = phdr[i].p_vaddr;
        image->tls_filesz = phdr[i].p_filesz;
        image->tls_memsz = phdr[i].p_memsz;
        image->tls_align = phdr[i].p_align;
        break;
      default:
        break;
    }
  }

  if (image->interp_path[0] != '\0') {
    elf32_log_error("elf32 PT_INTERP not supported yet: %s\n", image->interp_path);
    return -1;
  }
  if (image->phdr == 0) {
    u32 phdr_bytes = ehdr->e_phnum * ehdr->e_phentsize;
    u32 phdr_vaddr = elf32_align_up(max_vaddr, PAGE_SIZE);
    if (phdr_bytes == 0 ||
        valloc((void*)phdr_vaddr, PAGE_SIZE) == NULL) {
      elf32_log_error("elf32 install phdr failed bytes=%x vaddr=%x\n",
                      phdr_bytes, phdr_vaddr);
      return -1;
    }
    thread_t* current = thread_current();
    if (elf32_user_ptr(current, (void*)phdr_vaddr, phdr_bytes) == NULL) {
      elf32_log_error("elf32 install phdr map failed vaddr=%x\n", phdr_vaddr);
      return -1;
    }
    elf32_user_memcpy(current, (void*)phdr_vaddr, phdr, phdr_bytes);
    image->phdr = phdr_vaddr;
  }

  return 0;
}

static int elf32_open_and_load(const char* path, elf32_image_info_t* image) {
  Elf32_Ehdr ehdr;
  int fd = (int)sys_open_kernel(path, 0);
  if (fd < 0) {
    elf32_log_error("open elf32 %s failed\n", path);
    return -1;
  }
  u32 got = sys_read(fd, &ehdr, sizeof(ehdr));
  if (got != sizeof(ehdr) || !elf32_is_valid(ehdr.e_ident)) {
    elf32_log_error(
        "bad elf32 image %s got=%x ident=%x %x %x %x class=%x data=%x\n", path,
        got, ehdr.e_ident[EI_MAG0], ehdr.e_ident[EI_MAG1], ehdr.e_ident[EI_MAG2],
        ehdr.e_ident[EI_MAG3], ehdr.e_ident[EI_CLASS], ehdr.e_ident[EI_DATA]);
    sys_close(fd);
    return -1;
  }
  int ret = elf32_load_image_fd(fd, &ehdr, image);
  sys_close(fd);
  return ret;
}

static int elf32_build_initial_stack(thread_t* current, const exec_params_t* exec,
                                     const elf32_image_info_t* image,
                                     exec_stack_layout_t* layout) {
  const int auxc = 20;
  u32 top = current->ctx->usp;
#ifdef VM_ENABLE
  if (current->vm != NULL) {
    vmemory_area_t* vm_stack =
        vmemory_area_find_flag(current->vm->vma, MEMORY_STACK);
    if (vm_stack != NULL) {
      top = vm_stack->vend;
    }
  }
#endif
  u32 string_bytes = exec->string_bytes + kstrlen(exec->filename) + 1;
  const u32 random_bytes = 16;
  const u32 stack_pad = 64;
  u32 table_bytes =
      sizeof(u32) + sizeof(char*) * (exec->argc + 1) +
      sizeof(char*) * (exec->envc + 1) + sizeof(u32) * 2 * auxc;
  u32 total =
      elf32_align_up(string_bytes + random_bytes + table_bytes + stack_pad, 16);
  u32 bottom = top - current->ctx->usp_size;
  if (total >= current->ctx->usp_size) {
    elf32_log_error("elf32 stack too large total=%x stack=%x\n", total,
                    current->ctx->usp_size);
    return -1;
  }

  u32 sp = elf32_align_down(top - total, 16);
  if (sp < bottom) {
    elf32_log_error("elf32 stack overflow sp=%x bottom=%x\n", sp, bottom);
    return -1;
  }
  if (elf32_reset_user_stack(current, bottom, top) < 0) {
    elf32_log_error("elf32 reset user stack failed bottom=%x top=%x\n", bottom,
                    top);
    return -1;
  }

  char** argv_ptrs = NULL;
  char** envp_ptrs = NULL;
  if (exec->argc > 0) {
    argv_ptrs = kmalloc(sizeof(char*) * exec->argc, KERNEL_TYPE);
    if (argv_ptrs == NULL) {
      return -1;
    }
  }
  if (exec->envc > 0) {
    envp_ptrs = kmalloc(sizeof(char*) * exec->envc, KERNEL_TYPE);
    if (envp_ptrs == NULL) {
      if (argv_ptrs != NULL) kfree(argv_ptrs);
      return -1;
    }
  }

  char* strp = (char*)top;
  char* execfn = NULL;
  char* random = NULL;
  for (int i = exec->envc - 1; i >= 0; i--) {
    size_t len = kstrlen(exec->envp[i]) + 1;
    strp -= len;
    elf32_user_memcpy(current, strp, exec->envp[i], len);
    envp_ptrs[i] = strp;
  }
  for (int i = exec->argc - 1; i >= 0; i--) {
    size_t len = kstrlen(exec->argv[i]) + 1;
    strp -= len;
    elf32_user_memcpy(current, strp, exec->argv[i], len);
    argv_ptrs[i] = strp;
  }
  {
    size_t len = kstrlen(exec->filename) + 1;
    strp -= len;
    elf32_user_memcpy(current, strp, exec->filename, len);
    execfn = strp;
  }
  {
    strp -= random_bytes;
    strp = (char*)elf32_align_down((u32)strp, 4);
    random = strp;
    u8 seed[random_bytes];
    u32 tick = current != NULL ? current->id : 0;
    for (u32 i = 0; i < random_bytes; i++) {
      seed[i] = (u8)(tick ^ (tick >> (i & 7)) ^ (u32)execfn ^ i);
    }
    elf32_user_memcpy(current, random, seed, random_bytes);
  }

  u32* argc_at = (u32*)sp;
  char** argv_at = (char**)(argc_at + 1);
  char** envp_at = argv_at + exec->argc + 1;
  u32* auxv_at = (u32*)(envp_at + exec->envc + 1);
  u32 phdr_addr = image->phdr;
  if (phdr_addr == 0) {
    elf32_log_error("elf32 missing phdr addr file=%s\n", exec->filename);
    if (argv_ptrs != NULL) kfree(argv_ptrs);
    if (envp_ptrs != NULL) kfree(envp_ptrs);
    return -1;
  }

  elf32_user_memcpy(current, argc_at, &exec->argc, sizeof(u32));
  for (int i = 0; i < exec->argc; i++) {
    elf32_user_memcpy(current, &argv_at[i], &argv_ptrs[i], sizeof(char*));
  }
  {
    char* null = NULL;
    elf32_user_memcpy(current, &argv_at[exec->argc], &null, sizeof(char*));
  }
  for (int i = 0; i < exec->envc; i++) {
    elf32_user_memcpy(current, &envp_at[i], &envp_ptrs[i], sizeof(char*));
  }
  {
    char* null = NULL;
    elf32_user_memcpy(current, &envp_at[exec->envc], &null, sizeof(char*));
  }

  {
    u32 aux_pairs[2 * 20];
    u32 aux_idx = 0;
    elf32_auxv_put(aux_pairs, &aux_idx, AT_IGNORE, 0);
    elf32_auxv_put(aux_pairs, &aux_idx, AT_EXECFD, 0);
    elf32_auxv_put(aux_pairs, &aux_idx, AT_PAGESZ, PAGE_SIZE);
    elf32_auxv_put(aux_pairs, &aux_idx, AT_PHDR, phdr_addr);
    elf32_auxv_put(aux_pairs, &aux_idx, AT_PHENT, image->phent);
    elf32_auxv_put(aux_pairs, &aux_idx, AT_PHNUM, image->phnum);
    elf32_auxv_put(aux_pairs, &aux_idx, AT_BASE, 0);
    elf32_auxv_put(aux_pairs, &aux_idx, AT_FLAGS, 0);
    elf32_auxv_put(aux_pairs, &aux_idx, AT_ENTRY, image->entry);
    elf32_auxv_put(aux_pairs, &aux_idx, AT_NOTELF, 0);
    elf32_auxv_put(aux_pairs, &aux_idx, AT_UID, 0);
    elf32_auxv_put(aux_pairs, &aux_idx, AT_EUID, 0);
    elf32_auxv_put(aux_pairs, &aux_idx, AT_GID, 0);
    elf32_auxv_put(aux_pairs, &aux_idx, AT_EGID, 0);
    elf32_auxv_put(aux_pairs, &aux_idx, AT_HWCAP, 0);
    elf32_auxv_put(aux_pairs, &aux_idx, AT_CLKTCK, 100);
    elf32_auxv_put(aux_pairs, &aux_idx, AT_RANDOM, (u32)random);
    elf32_auxv_put(aux_pairs, &aux_idx, AT_EXECFN, (u32)execfn);
    elf32_auxv_put(aux_pairs, &aux_idx, AT_NULL, 0);
    elf32_user_memcpy(current, auxv_at, aux_pairs, aux_idx * sizeof(u32));
  }

  layout->sp = sp;
  layout->stack_top = top;
  layout->argv = argv_at;
  layout->envp = envp_at;
  layout->auxv = (Elf32_auxv_t*)auxv_at;
  layout->execfn = execfn;

  elf32_user_cache_sync((void*)sp, top - sp);

  {
    u32 argc_val = 0;
    u32 argv0 = 0;
    u32 aux_off = (u32)((char*)auxv_at - (char*)sp);
    u32 table_end = (u32)((char*)auxv_at + sizeof(u32) * 2 * auxc);
    u32 verify_phdr = 0;
    u32 verify_random = 0;
    u32 verify_execfn = 0;
    u32 verify_entry = 0;
    u32 aux_buf[2] = {0, 0};
    elf32_user_read(current, argc_at, &argc_val, sizeof(argc_val));
    if (exec->argc > 0) {
      elf32_user_read(current, &argv_at[0], &argv0, sizeof(argv0));
    }
    for (u32 i = 0; i < (u32)auxc; i++) {
      if (elf32_user_read(current, &auxv_at[i * 2], aux_buf, sizeof(aux_buf)) < 0) {
        break;
      }
      if (aux_buf[0] == 0) {
        break;
      }
      if (aux_buf[0] == AT_PHDR) {
        verify_phdr = aux_buf[1];
      } else if (aux_buf[0] == AT_RANDOM) {
        verify_random = aux_buf[1];
      } else if (aux_buf[0] == AT_EXECFN) {
        verify_execfn = aux_buf[1];
      } else if (aux_buf[0] == AT_ENTRY) {
        verify_entry = aux_buf[1];
      }
    }
    elf32_log_debug(
        "elf32 stack argc=%d envc=%d argv0=%x sp=%x aux=%x table_end=%x "
        "random=%x execfn=%x\n",
        argc_val, exec->envc, argv0, sp, aux_off, table_end, (u32)random,
        (u32)execfn);
    elf32_log_debug(
        "elf32 aux verify phdr=%x phent=%x phnum=%x entry=%x random=%x execfn=%x\n",
        verify_phdr, image->phent, image->phnum, verify_entry, verify_random,
        verify_execfn);
    if (verify_phdr != phdr_addr) {
      elf32_log_error("elf32 aux AT_PHDR mismatch want=%x got=%x\n", phdr_addr,
                      verify_phdr);
    }
    if (verify_random != (u32)random) {
      elf32_log_error("elf32 aux AT_RANDOM mismatch want=%x got=%x\n",
                      (u32)random, verify_random);
    }
    if (verify_execfn != (u32)execfn) {
      elf32_log_error("elf32 aux AT_EXECFN mismatch want=%x got=%x\n",
                      (u32)execfn, verify_execfn);
    }
    if (elf32_ptr_looks_like_ascii(verify_random)) {
      elf32_log_error("elf32 aux AT_RANDOM looks like ascii pointer %x\n",
                      verify_random);
    }
    if (table_end > (u32)random) {
      elf32_log_error("elf32 stack overlap table_end=%x random=%x\n", table_end,
                      (u32)random);
    }
  }

  if (argv_ptrs != NULL) kfree(argv_ptrs);
  if (envp_ptrs != NULL) kfree(envp_ptrs);
  return 0;
}

static void elf32_enter_user(thread_t* current, const elf32_image_info_t* image,
                             const exec_stack_layout_t* layout) {
  if (layout->stack_top > layout->sp) {
    elf32_user_cache_sync((void*)(uintptr_t)layout->sp,
                          layout->stack_top - layout->sp);
  }

  thread_reset_user_context(current, (void*)(uintptr_t)image->entry,
                            (void*)(uintptr_t)layout->sp);
  if (current->ctx->ic != NULL && current->ctx->ksp != NULL) {
    kmemmove(current->ctx->ic, current->ctx->ksp, sizeof(interrupt_context_t));
  }
}

int run_elf_thread(long* p) {
  (void)p;
  thread_t* current = thread_current();
  if (current == NULL || current->exec == NULL) {
    elf32_log_error("run_elf_thread missing exec params\n");
    return -1;
  }
  elf32_log_debug("elf32 run elf thread\n");

  exec_params_t* exec = current->exec;
  elf32_image_info_t image;
  exec_stack_layout_t layout;
  kmemset(&image, 0, sizeof(image));
  kmemset(&layout, 0, sizeof(layout));

  if (elf32_open_and_load(exec->filename, &image) < 0) {
    elf32_log_debug("elf32 open and load failed\n");
    return -1;
  }
  if (elf32_build_initial_stack(current, exec, &image, &layout) < 0) {
    elf32_log_debug("elf32 build initial stack failed\n");
    return -1;
  }

  elf32_log_debug("elf32 start entry=%x sp=%x phdr=%x phnum=%d file=%s\n",
                  image.entry, layout.sp, image.phdr, image.phnum,
                  exec->filename);
  elf32_enter_user(current, &image, &layout);
  return 0;
}

void go_start(entry_fn entry, long* args) {
  (void)entry;
  (void)args;
}

void* load_elf_interp(char* filename, void* args) {
  (void)filename;
  (void)args;
  return NULL;
}
