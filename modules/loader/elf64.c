#include "kernel/kernel.h"
#include "kernel/elf.h"
#include "kernel/memory.h"
#include "kernel/thread.h"
#include "libs/include/kernel/string.h"
#include "loader.h"
#include "posix/sysfn.h"

#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4

#ifdef LOAD_ELF_DEBUG
#define elf64_log_debug kprintf
#else
#define elf64_log_debug(...)
#endif

#define elf64_log_error kprintf

typedef struct elf64_image_info {
  u64 base;
  u64 entry;
  u64 phdr;
  u64 phent;
  u64 phnum;
  u64 interp_base;
  u64 phdr_copy;
  char interp_path[MAX_INTERP_PATH];
} elf64_image_info_t;

typedef struct exec_stack_layout64 {
  u64 sp;
  u64 stack_top;
} exec_stack_layout64_t;

static u64 elf64_align_down(u64 val, u64 align) { return val & ~(align - 1); }

static u64 elf64_align_up(u64 val, u64 align) {
  return (val + align - 1) & ~(align - 1);
}

static void elf64_user_cache_sync(void* user_addr, u64 size) {
  if (user_addr == NULL || size == 0) {
    return;
  }
#if defined(ARM64) || defined(__aarch64__)
  uintptr_t start = (uintptr_t)user_addr & ~63UL;
  uintptr_t end = ((uintptr_t)user_addr + size + 63UL) & ~63UL;
  for (uintptr_t va = start; va < end; va += 64) {
    asm volatile("dc civac, %0" : : "r"(va) : "memory");
  }
  asm volatile("dsb ish" ::: "memory");
  asm volatile("isb" ::: "memory");
#else
  (void)user_addr;
  (void)size;
#endif
}

static void* elf64_user_ptr(thread_t* current, void* user_addr, u64 size) {
#ifdef VM_ENABLE
  if (current != NULL && current->vm != NULL && user_addr != NULL && size > 0) {
    u64 start = (u64)(uintptr_t)user_addr & ~(PAGE_SIZE - 1);
    u64 end = elf64_align_up((u64)(uintptr_t)user_addr + size, PAGE_SIZE);
    for (u64 va = start; va < end; va += PAGE_SIZE) {
      if (page_v2p(current->vm->upage, (void*)(uintptr_t)va) == NULL) {
        if (valloc((void*)(uintptr_t)va, PAGE_SIZE) == NULL) {
          elf64_log_error("elf64 valloc user page failed va=%lx\n", va);
          return NULL;
        }
      }
    }
  }
#endif
  return user_addr;
}

static void* elf64_user_access(thread_t* current, void* user_addr, u64 size) {
  if (user_addr == NULL || size == 0) {
    return NULL;
  }
  if (elf64_user_ptr(current, user_addr, size) == NULL) {
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

static void elf64_user_memset(thread_t* current, void* user_dst, int val,
                              u64 size) {
  if (size == 0 || user_dst == NULL) {
    return;
  }
  void* dst = elf64_user_access(current, user_dst, size);
  if (dst == NULL) {
    elf64_log_error("elf64 user memset map failed dst=%lx size=%lx\n",
                    (u64)(uintptr_t)user_dst, size);
    return;
  }
  kmemset(dst, val, size);
  elf64_user_cache_sync(user_dst, size);
}

static void elf64_user_memcpy(thread_t* current, void* user_dst, const void* src,
                              u64 size) {
  if (size == 0 || user_dst == NULL || src == NULL) {
    return;
  }
  void* dst = elf64_user_access(current, user_dst, size);
  if (dst == NULL) {
    elf64_log_error("elf64 user memcpy map failed dst=%lx size=%lx\n",
                    (u64)(uintptr_t)user_dst, size);
    return;
  }
  kmemcpy(dst, src, size);
  elf64_user_cache_sync(user_dst, size);
}

static int elf64_reset_user_stack(thread_t* current, u64 bottom, u64 top) {
  if (current == NULL || top <= bottom) {
    return -1;
  }
  for (u64 va = bottom; va < top; va += PAGE_SIZE) {
    if (valloc((void*)(uintptr_t)va, PAGE_SIZE) == NULL) {
      elf64_log_error("elf64 reset stack page failed va=%lx\n", va);
      return -1;
    }
  }
  elf64_user_memset(current, (void*)(uintptr_t)bottom, 0, top - bottom);
  return 0;
}

static int elf64_is_valid(const unsigned char* e_ident) {
  return e_ident[EI_MAG0] == ELFMAG0 && e_ident[EI_MAG1] == ELFMAG1 &&
         e_ident[EI_MAG2] == ELFMAG2 && e_ident[EI_MAG3] == ELFMAG3 &&
         e_ident[EI_CLASS] == ELFCLASS64 && e_ident[EI_DATA] == ELFDATA2LSB;
}

static u64 elf64_reserve_bias(u64 min_vaddr, u64 max_vaddr) {
  vmemory_area_t* exec = vmemory_area_find_flag(thread_current()->vm->vma, MEMORY_EXEC);
  if (exec == NULL) {
    return 0;
  }

  u64 min_page = elf64_align_down(min_vaddr, PAGE_SIZE);
  u64 max_page = elf64_align_up(max_vaddr, PAGE_SIZE);
  u64 span = max_page - min_page;
  u64 load_base = elf64_align_up(exec->alloc_addr, PAGE_SIZE);

  exec->alloc_addr = load_base + span;
  exec->alloc_size += span;
  return load_base - min_page;
}

static int elf64_read_phdrs(int fd, const Elf64_Ehdr* ehdr, Elf64_Phdr* phdr) {
  syscall3(SYS_SEEK, fd, ehdr->e_phoff, 0);
  u32 need = sizeof(Elf64_Phdr) * ehdr->e_phnum;
  u32 got = syscall3(SYS_READ, fd, phdr, need);
  if (got != need) {
    elf64_log_error("read phdr failed, need=%d got=%d\n", need, got);
    return -1;
  }
  return 0;
}

static int elf64_scan_load_range(const Elf64_Ehdr* ehdr, const Elf64_Phdr* phdr,
                                 u64* min_vaddr, u64* max_vaddr) {
  int found = 0;
  *min_vaddr = ~0ULL;
  *max_vaddr = 0;

  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD || phdr[i].p_memsz == 0) {
      continue;
    }
    u64 seg_start = elf64_align_down(phdr[i].p_vaddr, PAGE_SIZE);
    u64 seg_end = elf64_align_up(phdr[i].p_vaddr + phdr[i].p_memsz, PAGE_SIZE);
    if (seg_start < *min_vaddr) *min_vaddr = seg_start;
    if (seg_end > *max_vaddr) *max_vaddr = seg_end;
    found = 1;
  }

  return found ? 0 : -1;
}

static u64 elf64_locate_phdr(const Elf64_Ehdr* ehdr, const Elf64_Phdr* phdr, u64 bias) {
  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_PHDR) {
      return bias + phdr[i].p_vaddr;
    }
  }

  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD) {
      continue;
    }
    u64 start = phdr[i].p_offset;
    u64 end = phdr[i].p_offset + phdr[i].p_filesz;
    if (ehdr->e_phoff >= start &&
        (ehdr->e_phoff + (u64)ehdr->e_phnum * ehdr->e_phentsize) <= end) {
      return bias + phdr[i].p_vaddr + (ehdr->e_phoff - phdr[i].p_offset);
    }
  }

  if (ehdr->e_phoff < PAGE_SIZE) {
    return bias + ehdr->e_phoff;
  }

  return 0;
}

static u64 elf64_copy_phdr_to_user(const Elf64_Ehdr* ehdr, const Elf64_Phdr* phdr) {
  vmemory_area_t* exec = vmemory_area_find_flag(thread_current()->vm->vma, MEMORY_EXEC);
  if (exec == NULL) {
    return 0;
  }

  u64 size = ehdr->e_phnum * ehdr->e_phentsize;
  u64 dst = elf64_align_up(exec->alloc_addr, 16);
  exec->alloc_addr = dst + size;
  exec->alloc_size += dst + size - dst;

  valloc((void*)dst, size);
  kmemcpy((void*)dst, phdr, size);
  return dst;
}

static int elf64_read_interp(int fd, const Elf64_Ehdr* ehdr, const Elf64_Phdr* phdr,
                             char* interp_path) {
  interp_path[0] = '\0';
  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type != PT_INTERP) {
      continue;
    }
    u32 len = (u32)phdr[i].p_filesz;
    if (len >= MAX_INTERP_PATH) {
      len = MAX_INTERP_PATH - 1;
    }
    syscall3(SYS_SEEK, fd, phdr[i].p_offset, 0);
    u32 got = syscall3(SYS_READ, fd, interp_path, len);
    if (got != len) {
      elf64_log_error("read interp failed %d %d\n", got, len);
      return -1;
    }
    interp_path[len] = '\0';
    return 0;
  }
  return 0;
}

static int elf64_map_segment(int fd, const Elf64_Phdr* ph) {
  if (ph->p_memsz == 0) {
    return 0;
  }

  u64 seg_page = elf64_align_down(ph->p_vaddr, PAGE_SIZE);
  u64 seg_end = elf64_align_up(ph->p_vaddr + ph->p_memsz, PAGE_SIZE);
  u64 seg_size = seg_end - seg_page;

  valloc((void*)seg_page, seg_size);
  kmemset((void*)seg_page, 0, seg_size);

  if (ph->p_filesz != 0) {
    syscall3(SYS_SEEK, fd, ph->p_offset, 0);
    u32 got = syscall3(SYS_READ, fd, (void*)ph->p_vaddr, (u32)ph->p_filesz);
    if (got != ph->p_filesz) {
      elf64_log_error("read load segment failed %lx %lx\n", got, ph->p_filesz);
      return -1;
    }
  }

  return 0;
}

static int elf64_load_image_fd(int fd, const Elf64_Ehdr* ehdr, elf64_image_info_t* image,
                               int force_dyn_bias) {
  Elf64_Phdr phdr[MAX_PHDR];
  u64 min_vaddr = 0;
  u64 max_vaddr = 0;

  if (ehdr->e_phnum > MAX_PHDR || ehdr->e_phentsize != sizeof(Elf64_Phdr)) {
    elf64_log_error("bad phdr table phnum=%d entsize=%d\n", ehdr->e_phnum,
                    ehdr->e_phentsize);
    return -1;
  }

  kmemset(phdr, 0, sizeof(phdr));
  if (elf64_read_phdrs(fd, ehdr, phdr) < 0) {
    return -1;
  }

  if (elf64_scan_load_range(ehdr, phdr, &min_vaddr, &max_vaddr) < 0) {
    elf64_log_error("no PT_LOAD in elf64 image\n");
    return -1;
  }

  u64 bias = 0;
  if (force_dyn_bias || ehdr->e_type == ET_DYN) {
    bias = elf64_reserve_bias(min_vaddr, max_vaddr);
  }

  image->base = bias;
  image->entry = bias + ehdr->e_entry;
  image->phent = ehdr->e_phentsize;
  image->phnum = ehdr->e_phnum;
  image->phdr = elf64_locate_phdr(ehdr, phdr, bias);
  image->phdr_copy = 0;
  image->interp_base = 0;

  if (elf64_read_interp(fd, ehdr, phdr, image->interp_path) < 0) {
    return -1;
  }

  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD) {
      continue;
    }

    Elf64_Phdr load = phdr[i];
    load.p_vaddr += bias;
    elf64_log_debug(
        "elf64 load seg[%d] off=%lx vaddr=%lx filesz=%lx memsz=%lx flags=%lx bias=%lx\n",
        i, phdr[i].p_offset, load.p_vaddr, phdr[i].p_filesz, phdr[i].p_memsz,
        phdr[i].p_flags, bias);
    if (elf64_map_segment(fd, &load) < 0) {
      return -1;
    }
  }

  if (image->phdr == 0 || image->phdr < PAGE_SIZE) {
    image->phdr_copy = elf64_copy_phdr_to_user(ehdr, phdr);
    image->phdr = image->phdr_copy;
  }

  return 0;
}

static int elf64_open_and_load(const char* path, elf64_image_info_t* image,
                               int force_dyn_bias) {
  Elf64_Ehdr ehdr;
  int fd = syscall2(SYS_OPEN, path, 0);
  if (fd < 0) {
    elf64_log_error("open elf64 %s failed\n", path);
    return -1;
  }

  kmemset(&ehdr, 0, sizeof(ehdr));
  u32 got = syscall3(SYS_READ, fd, &ehdr, sizeof(ehdr));
  if (got < sizeof(ehdr) || !elf64_is_valid(ehdr.e_ident) ||
      ehdr.e_version != EV_CURRENT) {
    elf64_log_error(
        "invalid elf64 image %s (class=%d machine=%d got=%d)\n", path,
        ehdr.e_ident[EI_CLASS], ehdr.e_machine, got);
    syscall1(SYS_CLOSE, fd);
    return -1;
  }
  if (ehdr.e_machine != 183) {
    elf64_log_error(
        "elf64 image %s has wrong arch machine=%d (need 183=aarch64, 40=arm)\n",
        path, ehdr.e_machine);
    syscall1(SYS_CLOSE, fd);
    return -1;
  }

  elf64_log_debug("elf64 open %s type=%d entry=%lx phoff=%lx phnum=%d\n", path,
                  ehdr.e_type, ehdr.e_entry, ehdr.e_phoff, ehdr.e_phnum);

  int ret = elf64_load_image_fd(fd, &ehdr, image, force_dyn_bias);
  elf64_log_debug(
      "elf64 image %s base=%lx entry=%lx phdr=%lx phent=%lx phnum=%lx interp=%s\n",
      path, image->base, image->entry, image->phdr, image->phent, image->phnum,
      image->interp_path[0] ? image->interp_path : "<none>");
  syscall1(SYS_CLOSE, fd);
  return ret;
}

static void elf64_auxv_put(u64** w, u64 type, u64 val) {
  *(*w)++ = type;
  *(*w)++ = val;
}

static int elf64_build_initial_stack(thread_t* current, const exec_params_t* exec,
                                     const elf64_image_info_t* image, u64 interp_base,
                                     exec_stack_layout64_t* layout) {
  const int auxc = 20;
  u64 top = current->ctx->usp;
#ifdef VM_ENABLE
  if (current->vm != NULL) {
    vmemory_area_t* vm_stack =
        vmemory_area_find_flag(current->vm->vma, MEMORY_STACK);
    if (vm_stack != NULL) {
      top = vm_stack->vend;
    }
  }
#endif
  u64 string_bytes = exec->string_bytes + kstrlen(exec->filename) + 1;
  const u64 random_bytes = 16;
  const u64 stack_pad = 64;
  u64 table_bytes = sizeof(u64) + sizeof(char*) * (exec->argc + 1) +
                    sizeof(char*) * (exec->envc + 1) + sizeof(u64) * 2 * auxc;
  u64 total =
      elf64_align_up(string_bytes + random_bytes + table_bytes + stack_pad, 16);
  u64 bottom = top - current->ctx->usp_size;
  if (total >= current->ctx->usp_size) {
    elf64_log_error("elf64 stack too large total=%lx stack=%lx\n", total,
                    current->ctx->usp_size);
    return -1;
  }

  u64 sp = elf64_align_down(top - total, 16);
  if (sp < bottom) {
    elf64_log_error("elf64 stack overflow sp=%lx bottom=%lx\n", sp, bottom);
    return -1;
  }
  if (elf64_reset_user_stack(current, bottom, top) < 0) {
    elf64_log_error("elf64 reset user stack failed bottom=%lx top=%lx\n", bottom,
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
      if (argv_ptrs != NULL) {
        kfree(argv_ptrs);
      }
      return -1;
    }
  }

  char* strp = (char*)(uintptr_t)top;
  char* execfn = NULL;
  char* random = NULL;
  for (int i = exec->envc - 1; i >= 0; i--) {
    size_t len = kstrlen(exec->envp[i]) + 1;
    strp -= len;
    elf64_user_memcpy(current, strp, exec->envp[i], len);
    envp_ptrs[i] = strp;
  }
  for (int i = exec->argc - 1; i >= 0; i--) {
    size_t len = kstrlen(exec->argv[i]) + 1;
    strp -= len;
    elf64_user_memcpy(current, strp, exec->argv[i], len);
    argv_ptrs[i] = strp;
  }
  {
    size_t len = kstrlen(exec->filename) + 1;
    strp -= len;
    elf64_user_memcpy(current, strp, exec->filename, len);
    execfn = strp;
  }
  {
    strp -= random_bytes;
    strp = (char*)elf64_align_down((u64)(uintptr_t)strp, 8);
    random = strp;
    u8 seed[random_bytes];
    u32 tick = current != NULL ? current->id : 0;
    for (u32 i = 0; i < random_bytes; i++) {
      seed[i] = (u8)(tick ^ (tick >> (i & 7)) ^ (u32)(uintptr_t)execfn ^ i);
    }
    elf64_user_memcpy(current, random, seed, random_bytes);
  }

  u64* argc_at = (u64*)(uintptr_t)sp;
  char** argv_at = (char**)(argc_at + 1);
  char** envp_at = argv_at + exec->argc + 1;
  u64* auxv_at = (u64*)(envp_at + exec->envc + 1);
  u64 phdr_addr = image->phdr;
  if (phdr_addr == 0) {
    elf64_log_error("elf64 missing phdr addr file=%s\n", exec->filename);
    if (argv_ptrs != NULL) {
      kfree(argv_ptrs);
    }
    if (envp_ptrs != NULL) {
      kfree(envp_ptrs);
    }
    return -1;
  }

  {
    u64 argc_val = (u64)exec->argc;
    elf64_user_memcpy(current, argc_at, &argc_val, sizeof(u64));
  }
  for (int i = 0; i < exec->argc; i++) {
    elf64_user_memcpy(current, &argv_at[i], &argv_ptrs[i], sizeof(char*));
  }
  {
    char* null = NULL;
    elf64_user_memcpy(current, &argv_at[exec->argc], &null, sizeof(char*));
  }
  for (int i = 0; i < exec->envc; i++) {
    elf64_user_memcpy(current, &envp_at[i], &envp_ptrs[i], sizeof(char*));
  }
  {
    char* null = NULL;
    elf64_user_memcpy(current, &envp_at[exec->envc], &null, sizeof(char*));
  }

  {
    u64 aux_pairs[2 * 20];
    u64* w = aux_pairs;
    elf64_auxv_put(&w, AT_IGNORE, 0);
    elf64_auxv_put(&w, AT_EXECFD, 0);
    elf64_auxv_put(&w, AT_PAGESZ, PAGE_SIZE);
    elf64_auxv_put(&w, AT_PHDR, phdr_addr);
    elf64_auxv_put(&w, AT_PHENT, image->phent);
    elf64_auxv_put(&w, AT_PHNUM, image->phnum);
    elf64_auxv_put(&w, AT_BASE, interp_base);
    elf64_auxv_put(&w, AT_FLAGS, 0);
    elf64_auxv_put(&w, AT_ENTRY, image->entry);
    elf64_auxv_put(&w, AT_NOTELF, 0);
    elf64_auxv_put(&w, AT_UID, 0);
    elf64_auxv_put(&w, AT_EUID, 0);
    elf64_auxv_put(&w, AT_GID, 0);
    elf64_auxv_put(&w, AT_EGID, 0);
    elf64_auxv_put(&w, AT_HWCAP, 0);
    elf64_auxv_put(&w, AT_CLKTCK, 100);
    elf64_auxv_put(&w, AT_RANDOM, (u64)(uintptr_t)random);
    elf64_auxv_put(&w, AT_EXECFN, (u64)(uintptr_t)execfn);
    elf64_auxv_put(&w, AT_NULL, 0);
    elf64_user_memcpy(current, auxv_at, aux_pairs,
                      (u64)(w - aux_pairs) * sizeof(u64));
  }

  layout->sp = sp;
  layout->stack_top = top;

  elf64_user_cache_sync((void*)(uintptr_t)sp, top - sp);

  if (argv_ptrs != NULL) {
    kfree(argv_ptrs);
  }
  if (envp_ptrs != NULL) {
    kfree(envp_ptrs);
  }
  return 0;
}

static void elf64_enter_user(thread_t* current, u64 entry,
                             const exec_stack_layout64_t* layout) {
  if (layout->stack_top > layout->sp) {
    elf64_user_cache_sync((void*)(uintptr_t)layout->sp,
                          layout->stack_top - layout->sp);
  }

  thread_reset_user_context(current, (void*)(uintptr_t)entry,
                            (void*)(uintptr_t)layout->sp);
  if (current->ctx->ic != NULL && current->ctx->ksp != NULL) {
    kmemmove(current->ctx->ic, current->ctx->ksp, sizeof(interrupt_context_t));
  }
}

int run_elf64_thread(long* p) {
  (void)p;
  thread_t* current = thread_current();
  if (current == NULL || current->exec == NULL) {
    elf64_log_error("run_elf64_thread missing exec params\n");
    return -1;
  }

  exec_params_t* exec = current->exec;
  elf64_image_info_t main_image;
  elf64_image_info_t interp_image;
  exec_stack_layout64_t layout;
  u64 start_entry;
  u64 interp_base = 0;

  kmemset(&main_image, 0, sizeof(main_image));
  kmemset(&interp_image, 0, sizeof(interp_image));
  kmemset(&layout, 0, sizeof(layout));

  if (elf64_open_and_load(exec->filename, &main_image, 0) < 0) {
    return -1;
  }

  start_entry = main_image.entry;
  if (main_image.interp_path[0] != '\0') {
    if (elf64_open_and_load(main_image.interp_path, &interp_image, 1) < 0) {
      elf64_log_error("load interp failed %s\n", main_image.interp_path);
      return -1;
    }
    interp_base = interp_image.base;
    main_image.interp_base = interp_base;
    start_entry = interp_image.entry;
  }

  if (elf64_build_initial_stack(current, exec, &main_image, interp_base, &layout) <
      0) {
    return -1;
  }

  elf64_log_debug(
      "elf64 start=%lx main_entry=%lx main_base=%lx interp_base=%lx phdr=%lx sp=%lx argc=%d file=%s\n",
      start_entry, main_image.entry, main_image.base, interp_base,
      main_image.phdr, layout.sp, exec->argc, exec->filename);
  elf64_enter_user(current, start_entry, &layout);
  return 0;
}
