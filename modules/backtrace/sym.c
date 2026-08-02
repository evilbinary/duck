/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
/* 懒加载 ELF 符号化器：
 * - 内核地址：fault 时读 /kernel.elf，首次解析后 symtab/strtab 缓存
 *   在静态缓冲区，之后内核 fault 变纯内存解析（懒 kallsyms）
 * - 用户地址：fault 时按线程名(ELF 路径)读应用文件，同一 dump 内复用
 * - 全程无 kmalloc，仅静态缓冲；VFS 锁被当前线程持有则放弃符号化
 */
#include "backtrace.h"

#include "kernel/elf.h"
#include "kernel/vfs.h"
#include "libs/include/kernel/string.h"

#define BT_KERNEL_ELF_PATH "/kernel.elf"

#ifndef BT_KERNEL_CACHE
#define BT_KERNEL_CACHE (64 * 1024)
#endif
#ifndef BT_APP_CACHE
#define BT_APP_CACHE (32 * 1024)
#endif
#define BT_SHDR_BUF_SIZE (8 * 1024)
#define BT_WIN_SIZE (4 * 1024)

static u8 bt_kernel_buf[BT_KERNEL_CACHE] __attribute__((aligned(8)));
static u8 bt_app_buf[BT_APP_CACHE] __attribute__((aligned(8)));
static u8 bt_shdr_buf[BT_SHDR_BUF_SIZE] __attribute__((aligned(8)));
static u8 bt_ehdr_buf[64] __attribute__((aligned(8)));
static u8 bt_win_buf[BT_WIN_SIZE] __attribute__((aligned(8)));

typedef struct bt_sym {
  u64 value;
  u64 size;
  u32 name;
  u32 type;
} bt_sym_t;

typedef struct bt_elf {
  const char* path;
  vnode_t* node;
  int parsed;
  int elf_class;
  u32 ent_size;
  u32 sym_off;
  u32 sym_size;
  u32 str_off;
  u32 str_size;
  u8* cache;         /* cached symtab (kernel 持久 / app 本次 dump) */
  u32 cache_cap;
  u32 cache_str_off; /* strtab 在缓冲内的偏移 */
} bt_elf_t;

static bt_elf_t bt_kernel_elf = {.path = BT_KERNEL_ELF_PATH,
                                 .cache = bt_kernel_buf,
                                 .cache_cap = sizeof(bt_kernel_buf)};
static bt_elf_t bt_app_elf = {.path = NULL,
                              .cache = bt_app_buf,
                              .cache_cap = sizeof(bt_app_buf)};

static int bt_read(vnode_t* node, u32 off, void* buf, u32 size) {
  if (node == NULL || node->op == NULL || node->op->read == NULL) {
    return -1;
  }
  u32 n = vread(node, off, size, buf);
  return n == size ? 0 : -1;
}

static int bt_elf_parse(bt_elf_t* e) {
  if (e->parsed) {
    return e->node != NULL ? 0 : -1;
  }
  e->parsed = 1;
  if (e->node == NULL) {
    return -1;
  }
  if (bt_read(e->node, 0, bt_ehdr_buf, sizeof(bt_ehdr_buf)) != 0) {
    return -1;
  }
  u8* ident = bt_ehdr_buf;
  if (!(ident[0] == 0x7f && ident[1] == 'E' && ident[2] == 'L' &&
        ident[3] == 'F')) {
    return -1;
  }
  e->elf_class = ident[EI_CLASS];
  u32 shoff, shnum, shentsize;
  if (e->elf_class == ELFCLASS32) {
    Elf32_Ehdr* eh = (Elf32_Ehdr*)bt_ehdr_buf;
    e->ent_size = 16;
    shoff = eh->e_shoff;
    shnum = eh->e_shnum;
    shentsize = eh->e_shentsize;
  } else if (e->elf_class == ELFCLASS64) {
    Elf64_Ehdr* eh = (Elf64_Ehdr*)bt_ehdr_buf;
    e->ent_size = 24;
    shoff = (u32)eh->e_shoff;
    shnum = (u32)eh->e_shnum;
    shentsize = (u32)eh->e_shentsize;
  } else {
    return -1;
  }
  if (shoff == 0 || shnum == 0 || shentsize == 0) {
    return -1;
  }
  u32 shdr_size = shnum * shentsize;
  if (shdr_size > BT_SHDR_BUF_SIZE) {
    return -1;
  }
  if (bt_read(e->node, shoff, bt_shdr_buf, shdr_size) != 0) {
    return -1;
  }
  if (e->elf_class == ELFCLASS32) {
    Elf32_Shdr* sh = (Elf32_Shdr*)bt_shdr_buf;
    for (u32 i = 0; i < shnum; i++) {
      if (sh[i].sh_type == SHT_SYMTAB && sh[i].sh_link < shnum) {
        e->sym_off = sh[i].sh_offset;
        e->sym_size = sh[i].sh_size;
        e->str_off = sh[sh[i].sh_link].sh_offset;
        e->str_size = sh[sh[i].sh_link].sh_size;
        break;
      }
    }
  } else {
    Elf64_Shdr* sh = (Elf64_Shdr*)bt_shdr_buf;
    for (u32 i = 0; i < shnum; i++) {
      if (sh[i].sh_type == SHT_SYMTAB && sh[i].sh_link < shnum) {
        e->sym_off = (u32)sh[i].sh_offset;
        e->sym_size = (u32)sh[i].sh_size;
        e->str_off = (u32)sh[sh[i].sh_link].sh_offset;
        e->str_size = (u32)sh[sh[i].sh_link].sh_size;
        break;
      }
    }
  }
  if (e->sym_size == 0 || e->str_size == 0) {
    return -1;
  }
  if (e->cache != NULL && e->sym_size + e->str_size <= e->cache_cap) {
    if (bt_read(e->node, e->sym_off, e->cache, e->sym_size) != 0 ||
        bt_read(e->node, e->str_off, e->cache + e->sym_size, e->str_size) !=
            0) {
      e->cache = NULL;
      return -1;
    }
    e->cache_str_off = e->sym_size;
  } else {
    e->cache = NULL; /* 超出缓存：退化为分窗文件扫描 */
  }
  return 0;
}

static void bt_take(u64 addr, bt_sym_t* s, bt_sym_t* best) {
  if (s->value == 0 || s->value > addr) {
    return;
  }
  if (s->type != STT_FUNC && s->type != STT_NOTYPE) {
    return;
  }
  if (s->name == 0) {
    return;
  }
  if (best->name == 0 || s->value > best->value ||
      (s->value == best->value && s->type == STT_FUNC &&
       best->type != STT_FUNC)) {
    *best = *s;
  }
}

static u32 bt_sym_from_buf(u8* buf, u32 ent_size, int elf_class, bt_sym_t* s) {
  if (elf_class == ELFCLASS32) {
    Elf32_Sym* p = (Elf32_Sym*)buf;
    s->value = p->st_value;
    s->size = p->st_size;
    s->name = p->st_name;
    s->type = ELF32_ST_TYPE(p->st_info);
  } else if (elf_class == ELFCLASS64) {
    Elf64_Sym* p = (Elf64_Sym*)buf;
    s->value = p->st_value;
    s->size = p->st_size;
    s->name = p->st_name;
    s->type = ELF64_ST_TYPE(p->st_info);
  } else {
    return -1;
  }
  (void)ent_size;
  return 0;
}

static int bt_lookup(bt_elf_t* e, u64 addr, bt_sym_t* best) {
  if (bt_elf_parse(e) != 0) {
    return -1;
  }
  if (e->cache != NULL) {
    u32 count = e->sym_size / e->ent_size;
    for (u32 i = 0; i < count; i++) {
      bt_sym_t s;
      if (bt_sym_from_buf(e->cache + i * e->ent_size, e->ent_size,
                          e->elf_class, &s) != 0) {
        break;
      }
      bt_take(addr, &s, best);
    }
  } else {
    u32 remaining = e->sym_size;
    u32 off = e->sym_off;
    while (remaining > 0) {
      u32 chunk = remaining < BT_WIN_SIZE ? remaining : BT_WIN_SIZE;
      if (bt_read(e->node, off, bt_win_buf, chunk) != 0) {
        break;
      }
      u32 nent = chunk / e->ent_size;
      for (u32 i = 0; i < nent; i++) {
        bt_sym_t s;
        if (bt_sym_from_buf(bt_win_buf + i * e->ent_size, e->ent_size,
                            e->elf_class, &s) != 0) {
          break;
        }
        bt_take(addr, &s, best);
      }
      off += chunk;
      remaining -= chunk;
    }
  }
  return best->name != 0 ? 0 : -1;
}

static void bt_name(bt_elf_t* e, u32 name_off, char* out, u32 out_size) {
  if (out_size == 0) {
    return;
  }
  u32 o = 0;
  if (e->cache != NULL && name_off < e->str_size) {
    u8* p = e->cache + e->cache_str_off + name_off;
    u32 remain = e->str_size - name_off;
    while (o + 1 < out_size && remain > 0 && p[0] != 0) {
      out[o++] = p[0];
      p++;
      remain--;
    }
    out[o] = 0;
    return;
  }
  u32 pos = e->str_off + name_off;
  u32 remain = e->str_size > name_off ? e->str_size - name_off : 0;
  while (o + 1 < out_size && remain > 0) {
    u32 chunk = remain < BT_WIN_SIZE ? remain : BT_WIN_SIZE;
    if (chunk > out_size - 1 - o) {
      chunk = out_size - 1 - o;
    }
    if (bt_read(e->node, pos, bt_win_buf, chunk) != 0) {
      break;
    }
    u32 i = 0;
    for (; i < chunk; i++) {
      if (bt_win_buf[i] == 0) {
        break;
      }
      out[o++] = bt_win_buf[i];
    }
    if (i < chunk) {
      break;
    }
    pos += chunk;
    remain -= chunk;
  }
  out[o] = 0;
}

static void bt_append_hex(char* out, u32 out_size, u64 v) {
  u32 len = kstrlen(out);
  if (len + 3 >= out_size) {
    return;
  }
  out[len++] = '+';
  out[len++] = '0';
  out[len++] = 'x';
  char tmp[16];
  int n = 0;
  do {
    tmp[n++] = "0123456789abcdef"[v & 0xf];
    v >>= 4;
  } while (v != 0 && n < 16);
  while (n > 0 && len + 1 < out_size) {
    out[len++] = tmp[--n];
  }
  out[len] = 0;
}

static void bt_format(bt_elf_t* e, bt_sym_t* s, u64 addr, char* out,
                      u32 out_size) {
  if (out_size == 0) {
    return;
  }
  bt_name(e, s->name, out, out_size);
  u64 off = addr - s->value;
  if (off > 0) {
    bt_append_hex(out, out_size, off);
  }
}

void bt_sym_lookup(thread_t* t, u32 addr, int mode, char* out,
                   u32 out_size) {
  if (out_size == 0) {
    return;
  }
  out[0] = 0;
  if (vfs_locked_by(t)) {
    return; /* 当前线程持 VFS 锁，异常路径禁止再入文件系统 */
  }
  if (mode == 3) {
    const char* path = t != NULL ? t->name : NULL;
    if (path == NULL || path[0] == 0) {
      return;
    }
    if (bt_app_elf.node == NULL || bt_app_elf.path != path) {
      bt_app_elf.path = path;
      bt_app_elf.parsed = 0;
      bt_app_elf.cache = bt_app_buf;
      bt_app_elf.cache_cap = sizeof(bt_app_buf);
      bt_app_elf.node = NULL;
      vnode_t* root = t->vfs != NULL ? t->vfs->root : NULL;
      vnode_t* pwd = t->vfs != NULL ? t->vfs->pwd : NULL;
      bt_app_elf.node = vfs_find_relative(root, pwd, path);
      if (bt_app_elf.node == NULL) {
        bt_app_elf.node = vfs_find(NULL, (u8*)path);
      }
      if (bt_app_elf.node == NULL) {
        return;
      }
    }
    bt_sym_t best;
    kmemset(&best, 0, sizeof(best));
    if (bt_lookup(&bt_app_elf, addr, &best) == 0) {
      bt_format(&bt_app_elf, &best, addr, out, out_size);
    }
  } else {
    if (bt_kernel_elf.node == NULL) {
      bt_kernel_elf.node = vfs_find(NULL, (u8*)BT_KERNEL_ELF_PATH);
      if (bt_kernel_elf.node == NULL) {
        return;
      }
    }
    bt_sym_t best;
    kmemset(&best, 0, sizeof(best));
    if (bt_lookup(&bt_kernel_elf, addr, &best) == 0) {
      bt_format(&bt_kernel_elf, &best, addr, out, out_size);
    }
  }
}
