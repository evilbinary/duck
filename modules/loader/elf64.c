#include "kernel/kernel.h"
#include "kernel/elf.h"
#include "kernel/memory.h"
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

static int elf64_is_valid(const unsigned char* e_ident) {
  return e_ident[EI_MAG0] == ELFMAG0 && e_ident[EI_MAG1] == ELFMAG1 &&
         e_ident[EI_MAG2] == ELFMAG2 && e_ident[EI_MAG3] == ELFMAG3 &&
         e_ident[EI_CLASS] == ELFCLASS64 && e_ident[EI_DATA] == ELFDATA2LSB;
}

static u64 elf64_align_down(u64 val, u64 align) { return val & ~(align - 1); }

static u64 elf64_align_up(u64 val, u64 align) {
  return (val + align - 1) & ~(align - 1);
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
      ehdr.e_machine != 183 || ehdr.e_version != EV_CURRENT) {
    elf64_log_error("invalid elf64 image %s\n", path);
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

static void elf64_build_auxv(void* auxv_at, const elf64_image_info_t* main_image,
                             u64 interp_base, const char* execfn) {
  u64* w = (u64*)auxv_at;

  elf64_auxv_put(&w, AT_IGNORE, 0);
  elf64_auxv_put(&w, AT_EXECFD, 0);
  elf64_auxv_put(&w, AT_PAGESZ, PAGE_SIZE);
  elf64_auxv_put(&w, AT_PHDR, main_image->phdr);
  elf64_auxv_put(&w, AT_PHENT, main_image->phent);
  elf64_auxv_put(&w, AT_PHNUM, main_image->phnum);
  elf64_auxv_put(&w, AT_BASE, interp_base);
  elf64_auxv_put(&w, AT_FLAGS, 0);
  elf64_auxv_put(&w, AT_ENTRY, main_image->entry);
  elf64_auxv_put(&w, AT_NOTELF, 0);
  elf64_auxv_put(&w, AT_UID, 0);
  elf64_auxv_put(&w, AT_EUID, 0);
  elf64_auxv_put(&w, AT_GID, 0);
  elf64_auxv_put(&w, AT_EGID, 0);
  elf64_auxv_put(&w, AT_PLATFORM, 0);
  elf64_auxv_put(&w, AT_HWCAP, 0);
  elf64_auxv_put(&w, AT_CLKTCK, 0);
  if (execfn != NULL) {
    elf64_auxv_put(&w, AT_EXECFN, (u64)execfn);
  }
  elf64_auxv_put(&w, AT_NULL, 0);
}

__attribute__((noreturn)) static void elf64_enter(u64 entry, long* sp0, long argc,
                                                  char** argv, char** envp,
                                                  void* auxv) {
#if defined(ARM64) || defined(__aarch64__)
  (void)argc;
  (void)argv;
  (void)envp;
  (void)auxv;
  u64 tp = sys_thread_self();
  elf64_log_debug("elf64 enter entry=%lx sp=%lx tp=%lx auxv=%lx\n", entry, sp0,
                  tp, auxv);
  asm volatile(
      "msr tpidr_el0, %6\n"
      "mov sp,  %0\n"
      "mov x0,  %0\n"
      "mov x1,  xzr\n"
      "mov x2,  xzr\n"
      "mov x3,  xzr\n"
      "br  %5\n"
      :
      : "r"(sp0), "r"(argc), "r"(argv), "r"(envp), "r"(auxv), "r"(entry), "r"(tp)
      : "memory");
#else
  ((entry_fn)entry)(sp0);
#endif
  __builtin_unreachable();
}

void run_elf64_thread(long* p) {
  elf64_log_debug("run_elf64_thread %lx\n", p);

  int argc;
  char** argv;
  char** envp0;
  char** auxv;
  char* filename;
  elf64_image_info_t main_image;
  elf64_image_info_t interp_image;
  u64 start_entry;
  if (p == NULL) {
    elf64_log_error("run_elf64_thread args is null\n");
    syscall1(SYS_EXIT, -1);
    return;
  }

  argc = p[0];
  argv = (char**)(p + 1);
  envp0 = argv + argc + 1;
  auxv = envp0;
  while (*auxv != NULL) {
    auxv++;
  }
  auxv++;

  filename = (char*)p[1];
  if (filename == NULL) {
    elf64_log_error("exec filename is null\n");
    syscall1(SYS_EXIT, -1);
    return;
  }

  kmemset(&main_image, 0, sizeof(main_image));
  kmemset(&interp_image, 0, sizeof(interp_image));

  if (elf64_open_and_load(filename, &main_image, 0) < 0) {
    syscall1(SYS_EXIT, -1);
    return;
  }

  start_entry = main_image.entry;
  if (main_image.interp_path[0] != '\0') {
    if (elf64_open_and_load(main_image.interp_path, &interp_image, 1) < 0) {
      elf64_log_error("load interp failed %s\n", main_image.interp_path);
      syscall1(SYS_EXIT, -1);
      return;
    }
    main_image.interp_base = interp_image.base;
    start_entry = interp_image.entry;
  }

  if (main_image.phdr == 0) {
    elf64_log_error("elf64 phdr is null for %s\n", filename);
  }

  elf64_build_auxv(auxv, &main_image, main_image.interp_base, filename);
  elf64_log_debug(
      "elf64 start=%lx main_entry=%lx main_base=%lx interp_base=%lx phdr=%lx sp=%lx argc=%d file=%s\n",
      start_entry, main_image.entry, main_image.base, main_image.interp_base,
      main_image.phdr, p, argc, filename);
  elf64_enter(start_entry, p, argc, argv, envp0, auxv);
}
