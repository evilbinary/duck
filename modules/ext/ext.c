/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 *
 * ext2/ext3/ext4 只读挂载。分区设备由 partition 模块提供（扇区 0 即该分区起点）。
 * 支持 extent 与传统间接块。不写日志，所以不提供写。
 ********************************************************************/
#include "kernel/device.h"
#include "kernel/stat.h"
#include "kernel/string.h"
#include "modules/sysconf/sysconf.h"

#if defined(ARM) || defined(ARM64) || defined(__aarch64__)
#define EXT_IOC_MAGIC 's'
#else
#define EXT_IOC_MAGIC 'a'
#endif
#define EXT_IOC_WRITE_OFFSET64 _IOW(EXT_IOC_MAGIC, 5, long)

#define EXT_MAGIC 0xEF53
#define EXT_INCOMPAT_FILETYPE 0x0002
#define EXT_INCOMPAT_RECOVER 0x0004
#define EXT_INCOMPAT_64BIT 0x0080
#define EXT_EXTENTS_FL 0x00080000
#define EXT_INDEX_FL 0x00001000
#define EXT_MAX_BLOCK (64 * 1024)
#define EXT_LINK_MAX 8

#define DT_UNKNOWN 0
#define DT_REG 8
#define DT_DIR 4
#define DT_LNK 10

typedef struct ext_fs {
  device_t *dev;
  u32 block_size;
  u32 inode_size;
  u32 inodes_per_group;
  u32 blocks_per_group;
  u32 first_data_block;
  u32 desc_size;
  u32 groups;
  u64 blocks_count;
  u32 incompat;
  int filetype;
  u8 *blk;
  u64 blk_lba;
  u8 *map;
  u8 *ino;
} ext_fs_t;

typedef struct ext_node {
  ext_fs_t *fs;
  u32 ino;
  u16 mode;
  u32 flags;
  u64 size;
  u32 iblock[15];
  u32 dir_index;
  u64 dir_byte;
  u32 dx_lblock;
} ext_node_t;

typedef struct ext_part {
  char name[8];
  device_t *dev;
  vnode_t *root;
  int mounted;
} ext_part_t;

static ext_part_t ext_parts[4];
static int ext_part_count;

/* 只够定位一次 ls。打完就停，避免每个文件名都刷串口。 */
static int ext_dbg_left = 48;

static int ext_dbg_ok(void) {
  if (ext_dbg_left <= 0) {
    return 0;
  }
  ext_dbg_left--;
  return 1;
}

static u16 r16(const u8 *p) { return (u16)p[0] | ((u16)p[1] << 8); }

static u32 r32(const u8 *p) {
  return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static int ext_dev_read(device_t *dev, u64 off, void *buf, u32 len) {
  u8 *p = buf;
  if (dev == NULL || dev->read == NULL || buf == NULL || len == 0) {
    return -1;
  }
  /* 整块 4096 直接读会失败，挂载用的小读却是好的。按扇区拆开。 */
  while (len > 0) {
    u32 chunk = len > 512 ? 512 : len;
    size_t n;
    if (dev->ioctl != NULL) {
      dev->ioctl(dev, EXT_IOC_WRITE_OFFSET64, off);
    }
    n = dev->read(dev, p, chunk);
    if (n != (size_t)chunk) {
      if (ext_dbg_ok()) {
        log_info("ext: read off=%x chunk=%u got=%x\n", (u32)off, chunk, (u32)n);
      }
      return -1;
    }
    p += chunk;
    off += chunk;
    len -= chunk;
  }
  return 0;
}

static int ext_extent_find(ext_fs_t *fs, u8 *hdr, u32 limit, u32 lblock,
                           u64 *pblock, int guard) {
  u16 magic;
  u16 entries;
  u16 depth;
  u32 i;
  u8 *e;
  if (hdr == NULL || guard > 6 || limit < 12) {
    return -1;
  }
  magic = r16(hdr);
  entries = r16(hdr + 2);
  /* eh_max 在 +4，eh_depth 在 +6。之前把 max 当成了深度。 */
  depth = r16(hdr + 6);
  if (magic != 0xF30A) {
    return -1;
  }
  if ((u32)entries * 12u + 12u > limit) {
    return -1;
  }
  e = hdr + 12;
  if (depth == 0) {
    for (i = 0; i < entries; i++, e += 12) {
      u32 start = r32(e);
      u16 len = r16(e + 4);
      u16 hi = r16(e + 6);
      u32 lo = r32(e + 8);
      u16 nblk = (u16)(len & 0x7fff);
      if (lblock >= start && (u32)(lblock - start) < nblk) {
        if (len & 0x8000) {
          *pblock = 0;
          return 0;
        }
        *pblock = (((u64)hi << 32) | lo) + (u64)(lblock - start);
        return 1;
      }
    }
    *pblock = 0;
    return 0;
  }
  {
    u8 *chosen = NULL;
    for (i = 0; i < entries; i++, e += 12) {
      if (r32(e) <= lblock) {
        chosen = e;
      } else {
        break;
      }
    }
    if (chosen == NULL) {
      *pblock = 0;
      return 0;
    }
    {
      u32 lo = r32(chosen + 4);
      u16 hi = r16(chosen + 8);
      u64 pb = ((u64)hi << 32) | lo;
      if (ext_dev_read(fs->dev, pb * fs->block_size, fs->map, fs->block_size) !=
          0) {
        return -1;
      }
      return ext_extent_find(fs, fs->map, fs->block_size, lblock, pblock,
                             guard + 1);
    }
  }
}

static int ext_ptr(ext_fs_t *fs, u32 block, u32 index, u64 *out) {
  if (block == 0) {
    *out = 0;
    return 0;
  }
  if ((u64)index * 4u + 4u > fs->block_size) {
    return -1;
  }
  if (ext_dev_read(fs->dev, (u64)block * fs->block_size, fs->map,
                   fs->block_size) != 0) {
    return -1;
  }
  *out = r32(fs->map + index * 4);
  return *out ? 1 : 0;
}

static int ext_bmap(ext_node_t *n, u64 lblock, u64 *pblock) {
  ext_fs_t *fs;
  u64 ptrs;
  u64 v;
  if (n == NULL || n->fs == NULL || pblock == NULL || lblock > 0xffffffffull) {
    return -1;
  }
  fs = n->fs;
  if (n->flags & EXT_EXTENTS_FL) {
    u8 raw[60];
    u32 i;
    for (i = 0; i < 15; i++) {
      raw[i * 4 + 0] = (u8)(n->iblock[i]);
      raw[i * 4 + 1] = (u8)(n->iblock[i] >> 8);
      raw[i * 4 + 2] = (u8)(n->iblock[i] >> 16);
      raw[i * 4 + 3] = (u8)(n->iblock[i] >> 24);
    }
    return ext_extent_find(fs, raw, 60, (u32)lblock, pblock, 0);
  }
  ptrs = fs->block_size / 4;
  v = lblock;
  if (v < 12) {
    *pblock = n->iblock[v];
    return *pblock ? 1 : 0;
  }
  v -= 12;
  if (v < ptrs) {
    return ext_ptr(fs, n->iblock[12], (u32)v, pblock);
  }
  v -= ptrs;
  if (v < ptrs * ptrs) {
    u64 mid;
    u32 d = (u32)(v / ptrs);
    u32 r = (u32)(v % ptrs);
    if (ext_ptr(fs, n->iblock[13], d, &mid) <= 0) {
      *pblock = 0;
      return 0;
    }
    return ext_ptr(fs, (u32)mid, r, pblock);
  }
  v -= ptrs * ptrs;
  {
    u64 per2 = ptrs * ptrs;
    u32 a = (u32)(v / per2);
    u64 rest = v % per2;
    u32 d = (u32)(rest / ptrs);
    u32 r = (u32)(rest % ptrs);
    u64 mid;
    u64 leaf;
    if (ext_ptr(fs, n->iblock[14], a, &mid) <= 0) {
      *pblock = 0;
      return 0;
    }
    if (ext_ptr(fs, (u32)mid, d, &leaf) <= 0) {
      *pblock = 0;
      return 0;
    }
    return ext_ptr(fs, (u32)leaf, r, pblock);
  }
}

static int ext_read_inode(ext_fs_t *fs, u32 ino, ext_node_t *n) {
  u32 group;
  u32 index;
  u32 i;
  u64 table;
  u64 ioff;
  u8 desc[64];
  u16 mode;
  if (fs == NULL || n == NULL || ino == 0 || fs->inodes_per_group == 0) {
    return -1;
  }
  group = (ino - 1) / fs->inodes_per_group;
  index = (ino - 1) % fs->inodes_per_group;
  if (group >= fs->groups) {
    return -1;
  }
  {
    u64 gdt = ((u64)fs->first_data_block + 1ull) * fs->block_size;
    u64 off = gdt + (u64)group * fs->desc_size;
    if (fs->desc_size > sizeof(desc)) {
      return -1;
    }
    if (ext_dev_read(fs->dev, off, desc, fs->desc_size) != 0) {
      return -1;
    }
  }
  table = r32(desc + 8);
  if (fs->desc_size >= 64) {
    table |= (u64)r32(desc + 40) << 32;
  }
  if (table == 0) {
    return -1;
  }
  ioff = table * fs->block_size + (u64)index * fs->inode_size;
  if (ext_dev_read(fs->dev, ioff, fs->ino, fs->inode_size) != 0) {
    return -1;
  }
  mode = r16(fs->ino);
  kmemset(n, 0, sizeof(*n));
  n->fs = fs;
  n->ino = ino;
  n->mode = mode;
  n->flags = r32(fs->ino + 0x20);
  n->size = r32(fs->ino + 4);
  n->dx_lblock = 0xffffffffu;
  if (fs->inode_size >= 0x70 && S_ISREG(mode)) {
    n->size |= (u64)r32(fs->ino + 0x6C) << 32;
  }
  for (i = 0; i < 15; i++) {
    n->iblock[i] = r32(fs->ino + 0x28 + i * 4);
  }
  if (r16(fs->ino + 0x28) == 0xF30A) {
    n->flags |= EXT_EXTENTS_FL;
  }
  return 0;
}

static u32 ext_pread(ext_node_t *n, u64 off, void *buf, u32 len) {
  u8 *out = buf;
  u32 done = 0;
  if (n == NULL || buf == NULL) {
    return 0;
  }
  if (off >= n->size) {
    return 0;
  }
  if ((u64)len > n->size - off) {
    len = (u32)(n->size - off);
  }
  if (S_ISLNK(n->mode) && (n->flags & EXT_EXTENTS_FL) == 0 && n->size <= 60) {
    u32 i;
    u8 raw[60];
    if (off >= 60) {
      return 0;
    }
    for (i = 0; i < 15; i++) {
      raw[i * 4 + 0] = (u8)n->iblock[i];
      raw[i * 4 + 1] = (u8)(n->iblock[i] >> 8);
      raw[i * 4 + 2] = (u8)(n->iblock[i] >> 16);
      raw[i * 4 + 3] = (u8)(n->iblock[i] >> 24);
    }
    if (off + len > n->size) {
      len = (u32)(n->size - off);
    }
    kmemcpy(out, raw + off, len);
    return len;
  }
  while (done < len) {
    u64 lblock = (off + done) / n->fs->block_size;
    u32 boff = (u32)((off + done) % n->fs->block_size);
    u32 chunk = n->fs->block_size - boff;
    u64 pb;
    int map;
    if (chunk > len - done) {
      chunk = len - done;
    }
    map = ext_bmap(n, lblock, &pb);
    if (map < 0) {
      break;
    }
    if (map == 0 || pb == 0) {
      kmemset(out + done, 0, chunk);
    } else if (ext_dev_read(n->fs->dev, pb * n->fs->block_size + boff,
                            out + done, chunk) != 0) {
      break;
    }
    done += chunk;
  }
  return done;
}

static void ext_dbg_bytes(const char *tag, const u8 *p, u32 n) {
  char line[80];
  u32 i;
  int pos = 0;
  if (!ext_dbg_ok() || p == NULL) {
    return;
  }
  if (n > 16) {
    n = 16;
  }
  for (i = 0; i < n && pos < (int)sizeof(line) - 4; i++) {
    pos += ksnprintf(line + pos, sizeof(line) - (u32)pos, "%02x ", p[i]);
  }
  log_info("ext: %s %s\n", tag, line);
}

static int ext_load_lblock(ext_node_t *dir, u32 lblock, u8 *dst) {
  u64 pb;
  int map;
  if (dir == NULL || dir->fs == NULL || dst == NULL) {
    return -1;
  }
  map = ext_bmap(dir, lblock, &pb);
  if (map <= 0 || pb == 0) {
    if (ext_dbg_ok()) {
      log_info("ext: load ino=%u lblock=%x map=%d pb=%x\n", dir->ino, lblock,
               map, (u32)pb);
    }
    return -1;
  }
  if (ext_dev_read(dir->fs->dev, pb * dir->fs->block_size, dst,
                   dir->fs->block_size) != 0) {
    if (ext_dbg_ok()) {
      log_info("ext: read ino=%u lblock=%x pb=%x len=%u failed\n", dir->ino,
               lblock, (u32)pb, dir->fs->block_size);
    }
    return -1;
  }
  if (ext_dbg_left > 40) {
    if (ext_dbg_ok()) {
      log_info("ext: load ino=%u lblock=%x pb=%x\n", dir->ino, lblock, (u32)pb);
    }
    ext_dbg_bytes("head", dst, 16);
  }
  return 0;
}

/* 解析一条目录项。reclen 写出。名字对不上时换另一种 name_len 宽度再试。 */
static int ext_parse_dirent(ext_fs_t *fs, u8 *e, u32 off, u32 bs, u32 *ino,
                            u32 *nlen, u8 *ft, u16 *reclen) {
  u16 rl;
  u32 n;
  u8 type;
  int wide;
  if (off + 8 > bs) {
    return -1;
  }
  rl = r16(e + 4);
  if (rl < 8 || off + rl > bs) {
    return -1;
  }
  *ino = r32(e);
  wide = fs->filetype ? 0 : 1;
  for (;;) {
    if (wide) {
      n = r16(e + 6);
      type = 0;
    } else {
      n = e[6];
      type = e[7];
    }
    if (n <= (u32)rl - 8) {
      *nlen = n;
      if (ft != NULL) {
        *ft = type;
      }
      *reclen = rl;
      return 0;
    }
    if (wide == (fs->filetype ? 0 : 1)) {
      wide = !wide;
      continue;
    }
    return -1;
  }
}

/* which 从 0 起。返回 1 找到，0 没有第 which 个叶子，-2 这不是 htree。 */
static int ext_dx_nth_leaf(ext_node_t *dir, u32 lblock, int is_root, int levels,
                           u32 *which, u32 *out) {
  u8 *blk;
  u32 bs;
  u32 ent;
  u16 count;
  u32 i;
  int rc;
  if (dir == NULL || which == NULL || out == NULL || dir->fs == NULL) {
    return -2;
  }
  bs = dir->fs->block_size;
  if (bs < 64 || levels > 3) {
    return -2;
  }
  blk = kmalloc(bs, KERNEL_TYPE);
  if (blk == NULL) {
    return -1;
  }
  if (ext_load_lblock(dir, lblock, blk) != 0) {
    kfree(blk);
    return -1;
  }
  if (is_root) {
    u16 dot;
    u8 info_len;
    if (r32(blk) == 0 || blk[8] != '.') {
      if (ext_dbg_ok()) {
        log_info("ext: htree reject ino=%u b0=%x b8=%x\n", dir->ino, r32(blk),
                 blk[8]);
      }
      kfree(blk);
      return -2;
    }
    dot = r16(blk + 4);
    if (dot < 12 || (u32)dot + 16 >= bs) {
      if (ext_dbg_ok()) {
        log_info("ext: htree dot reclen=%x bs=%u\n", dot, bs);
      }
      kfree(blk);
      return -2;
    }
    /* dot 之后是 dotdot（12 字节），再是 dx_root_info。 */
    if (blk[dot + 12 + 5] != 8) {
      if (ext_dbg_ok()) {
        log_info("ext: htree info_len=%x at %x\n", blk[dot + 12 + 5],
                 (u32)dot + 12);
      }
      kfree(blk);
      return -2;
    }
    info_len = blk[dot + 12 + 5];
    levels = blk[dot + 12 + 6];
    if (levels > 3) {
      kfree(blk);
      return -2;
    }
    ent = (u32)dot + 12 + info_len;
  } else {
    ent = 8;
  }
  if (ent + 8 > bs) {
    kfree(blk);
    return -2;
  }
  count = r16(blk + ent + 2);
  if (count < 2 || ent + (u32)count * 8u > bs) {
    if (ext_dbg_ok()) {
      log_info("ext: htree count=%u ent=%x bs=%u\n", count, ent, bs);
    }
    kfree(blk);
    return -2;
  }
  if (is_root && *which == 0 && ext_dbg_ok()) {
    log_info("ext: htree ino=%u levels=%u count=%u ent=%x\n", dir->ino, levels,
             count, ent);
  }
  rc = 0;
  for (i = 1; i < count; i++) {
    u32 child = r32(blk + ent + i * 8u + 4);
    if (levels == 0) {
      if (*which == 0) {
        *out = child;
        rc = 1;
        break;
      }
      (*which)--;
    } else {
      rc = ext_dx_nth_leaf(dir, child, 0, levels - 1, which, out);
      if (rc != 0) {
        break;
      }
    }
  }
  kfree(blk);
  return rc;
}

static int ext_htree_next(ext_node_t *dir, u64 *pos, u32 *ino, char *name,
                          u32 namemax, u8 *ft) {
  ext_fs_t *fs;
  u32 bs;
  u32 leaf_i;
  u32 off;
  u32 guard;
  fs = dir->fs;
  bs = fs->block_size;
  if (*pos == ~0ull) {
    return 0;
  }
  leaf_i = (u32)(*pos >> 32);
  off = (u32)*pos;
  for (guard = 0; guard < 4096; guard++) {
    u32 nth = leaf_i;
    u32 lblock = dir->dx_lblock;
    u8 *e;
    u16 reclen = 0;
    u32 nlen = 0;
    u32 next_off;
    int got = 1;
    /* 同一次列举里，叶子块还在缓存中就不用再走索引。查找会改掉 fs->blk。 */
    if (off == 0 || fs->blk_lba != lblock || lblock == 0xffffffffu) {
      got = ext_dx_nth_leaf(dir, 0, 1, 0, &nth, &lblock);
      if (got == -2) {
        return -2;
      }
      if (got != 1) {
        *pos = ~0ull;
        return 0;
      }
    }
    if (fs->blk_lba != lblock) {
      if (ext_load_lblock(dir, lblock, fs->blk) != 0) {
        leaf_i++;
        off = 0;
        dir->dx_lblock = 0xffffffffu;
        continue;
      }
      fs->blk_lba = lblock;
      dir->dx_lblock = lblock;
    }
    if (off >= bs || off + 8 > bs) {
      leaf_i++;
      off = 0;
      continue;
    }
    e = fs->blk + off;
    if (ext_parse_dirent(fs, e, off, bs, ino, &nlen, ft, &reclen) != 0) {
      leaf_i++;
      off = 0;
      continue;
    }
    next_off = off + reclen;
    if (next_off < off || next_off >= bs) {
      leaf_i++;
      next_off = 0;
    }
    *pos = ((u64)leaf_i << 32) | next_off;
    if (nlen >= namemax) {
      nlen = namemax - 1;
    }
    if (*ino != 0 && nlen > 0) {
      kmemcpy(name, e + 8, nlen);
      name[nlen] = 0;
      if (ext_dbg_ok()) {
        log_info("ext: ent ino=%u name=%s\n", *ino, name);
      }
    } else {
      name[0] = 0;
    }
    return 1;
  }
  *pos = ~0ull;
  return 0;
}

/* 返回 1 有条目，0 目录结束，-1 出错。inode 0 的条目也算 1，调用方跳过。 */
static int ext_dir_next(ext_node_t *dir, u64 *pos, u32 *ino, char *name,
                        u32 namemax, u8 *ft) {
  ext_fs_t *fs;
  u32 bs;
  if (dir == NULL || pos == NULL || dir->fs == NULL || namemax == 0) {
    return -1;
  }
  fs = dir->fs;
  bs = fs->block_size;
  if (bs == 0) {
    return -1;
  }
  /* 索引目录的文件名在叶子块里。块 0 只有 . 和 ..，线性扫会看成空目录。 */
  if ((dir->flags & EXT_INDEX_FL) != 0) {
    int rc;
    if (*pos == 0 && ext_dbg_ok()) {
      log_info("ext: dir ino=%u indexed size=%x flags=%x\n", dir->ino,
               (u32)dir->size, dir->flags);
    }
    rc = ext_htree_next(dir, pos, ino, name, namemax, ft);
    if (rc != -2) {
      return rc;
    }
    if (ext_dbg_ok()) {
      log_info("ext: ino=%u htree fallback linear\n", dir->ino);
    }
    dir->flags &= ~EXT_INDEX_FL;
    *pos = 0;
    fs->blk_lba = ~0ull;
  } else if (*pos == 0 && ext_dbg_ok()) {
    log_info("ext: dir ino=%u linear size=%x flags=%x\n", dir->ino,
             (u32)dir->size, dir->flags);
  }
  while (*pos < dir->size) {
    u32 off;
    u16 reclen = 0;
    u32 nlen = 0;
    u8 *e;
    u64 pb;
    u64 block = *pos / bs;
    off = (u32)(*pos % bs);
    if (off + 8 > bs) {
      *pos = (block + 1) * bs;
      continue;
    }
    if (fs->blk_lba != block) {
      int map = ext_bmap(dir, block, &pb);
      if (map <= 0 || pb == 0) {
        if (ext_dbg_ok()) {
          log_info("ext: bmap ino=%u block=%x map=%d pb=%x\n", dir->ino,
                   (u32)block, map, (u32)pb);
        }
        *pos = (block + 1) * bs;
        continue;
      }
      if (ext_dev_read(fs->dev, pb * bs, fs->blk, bs) != 0) {
        if (ext_dbg_ok()) {
          log_info("ext: dir block read ino=%u block=%x pb=%x\n", dir->ino,
                   (u32)block, (u32)pb);
        }
        *pos = (block + 1) * bs;
        continue;
      }
      fs->blk_lba = block;
      if (off == 0) {
        ext_dbg_bytes("dir", fs->blk, 16);
      }
    }
    e = fs->blk + off;
    if (ext_parse_dirent(fs, e, off, bs, ino, &nlen, ft, &reclen) != 0) {
      if (ext_dbg_ok()) {
        log_info("ext: bad ent ino=%u off=%x\n", dir->ino, off);
      }
      ext_dbg_bytes("bad", e, 16);
      *pos = (block + 1) * bs;
      continue;
    }
    if (nlen >= namemax) {
      nlen = namemax - 1;
    }
    if (*ino != 0 && nlen > 0) {
      kmemcpy(name, e + 8, nlen);
      name[nlen] = 0;
      if (ext_dbg_ok()) {
        log_info("ext: ent ino=%u name=%s\n", *ino, name);
      }
    } else {
      name[0] = 0;
    }
    *pos += reclen;
    return 1;
  }
  if (ext_dbg_ok()) {
    log_info("ext: dir ino=%u linear end pos=%x size=%x\n", dir->ino, (u32)*pos,
             (u32)dir->size);
  }
  return 0;
}

static int ext_lookup_one(ext_fs_t *fs, u32 dir_ino, const char *name,
                          u32 *out) {
  ext_node_t dir;
  u64 pos = 0;
  char ent[256];
  u32 ino;
  if (ext_read_inode(fs, dir_ino, &dir) != 0 || !S_ISDIR(dir.mode)) {
    return -1;
  }
  while (ext_dir_next(&dir, &pos, &ino, ent, sizeof(ent), NULL) == 1) {
    if (ino != 0 && kstrcmp(ent, name) == 0) {
      *out = ino;
      return 0;
    }
  }
  return -1;
}

static int ext_follow(ext_fs_t *fs, u32 dir_ino, u32 ino, ext_node_t *out,
                      int depth) {
  ext_node_t n;
  char *target;
  u32 got;
  u32 cur;
  char comp[256];
  u32 i;
  u32 k;
  if (depth > EXT_LINK_MAX) {
    return -1;
  }
  if (ext_read_inode(fs, ino, &n) != 0) {
    return -1;
  }
  if (!S_ISLNK(n.mode)) {
    *out = n;
    return 0;
  }
  if (n.size == 0 || n.size > 1023) {
    return -1;
  }
  target = kmalloc((u32)n.size + 1, KERNEL_TYPE);
  if (target == NULL) {
    return -1;
  }
  got = ext_pread(&n, 0, target, (u32)n.size);
  if (got != (u32)n.size) {
    kfree(target);
    return -1;
  }
  target[n.size] = 0;
  cur = (target[0] == '/') ? 2 : dir_ino;
  i = (target[0] == '/') ? 1 : 0;
  while (target[i] != 0) {
    while (target[i] == '/') {
      i++;
    }
    if (target[i] == 0) {
      break;
    }
    k = 0;
    while (target[i] != 0 && target[i] != '/' && k + 1 < sizeof(comp)) {
      comp[k++] = target[i++];
    }
    comp[k] = 0;
    if (k == 0) {
      break;
    }
    if (ext_lookup_one(fs, cur, comp, &ino) != 0) {
      kfree(target);
      return -1;
    }
    if (ext_read_inode(fs, ino, &n) != 0) {
      kfree(target);
      return -1;
    }
    if (S_ISLNK(n.mode)) {
      if (ext_follow(fs, cur, ino, &n, depth + 1) != 0) {
        kfree(target);
        return -1;
      }
      ino = n.ino;
    }
    cur = ino;
    if (S_ISDIR(n.mode)) {
      dir_ino = ino;
    }
  }
  kfree(target);
  *out = n;
  return 0;
}

static ext_node_t *ext_ensure(vnode_t *node) {
  ext_node_t *n;
  ext_node_t *src;
  if (node == NULL) {
    return NULL;
  }
  if (node->data != NULL) {
    return node->data;
  }
  if (node->super == NULL || node->super->data == NULL) {
    return NULL;
  }
  src = node->super->data;
  n = kmalloc(sizeof(*n), KERNEL_TYPE);
  if (n == NULL) {
    return NULL;
  }
  *n = *src;
  n->dir_index = 0;
  n->dir_byte = 0;
  node->data = n;
  return n;
}

static vnode_t *ext_op_find(vnode_t *node, char *name) {
  ext_node_t *dir;
  ext_node_t found;
  ext_node_t *copy;
  vnode_t *file;
  u32 ino;
  u32 type;
  if (name == NULL || name[0] == 0) {
    return NULL;
  }
  dir = ext_ensure(node);
  if (dir == NULL || !S_ISDIR(dir->mode)) {
    return NULL;
  }
  if (ext_lookup_one(dir->fs, dir->ino, name, &ino) != 0) {
    return NULL;
  }
  if (ext_follow(dir->fs, dir->ino, ino, &found, 0) != 0) {
    return NULL;
  }
  copy = kmalloc(sizeof(*copy), KERNEL_TYPE);
  if (copy == NULL) {
    return NULL;
  }
  *copy = found;
  type = S_ISDIR(found.mode) ? V_DIRECTORY : V_FILE;
  file = vfs_create_node((u8 *)name, type);
  if (file == NULL) {
    kfree(copy);
    return NULL;
  }
  file->data = copy;
  file->op = node->op;
  file->device = dir->fs->dev;
  file->length = (found.size > 0xffffffffull) ? 0xffffffffu : (u32)found.size;
  return file;
}

static u32 ext_op_read(vnode_t *node, u32 offset, u32 nbytes, u8 *buffer) {
  ext_node_t *n = ext_ensure(node);
  if (n == NULL || buffer == NULL || S_ISDIR(n->mode)) {
    return 0;
  }
  return ext_pread(n, offset, buffer, (u32)nbytes);
}

static u32 ext_op_write(vnode_t *node, u32 offset, u32 nbytes, u8 *buffer) {
  (void)node;
  (void)offset;
  (void)nbytes;
  (void)buffer;
  return (uint)-1;
}

static uint ext_op_open(vnode_t *node, uint mode) {
  (void)mode;
  return ext_ensure(node) != NULL ? 1 : 0;
}

static u32 ext_op_close(vnode_t *node) {
  (void)node;
  return 0;
}

static u8 ext_dtype(ext_fs_t *fs, u8 ft, u32 ino) {
  if (fs->filetype) {
    if (ft == 1) {
      return DT_REG;
    }
    if (ft == 2) {
      return DT_DIR;
    }
    if (ft == 7) {
      return DT_LNK;
    }
    return DT_UNKNOWN;
  }
  (void)ino;
  return DT_UNKNOWN;
}

static uint ext_op_readdir(vnode_t *node, struct vdirent *dirent, u32 *offset,
                           uint count) {
  ext_node_t *n;
  u64 byte;
  u32 index;
  u32 start;
  uint nbytes = 0;
  uint read_count = 0;
  if (node == NULL || dirent == NULL || offset == NULL || count == 0) {
    return 0;
  }
  n = ext_ensure(node);
  if (n == NULL || !S_ISDIR(n->mode)) {
    if (ext_dbg_ok()) {
      log_info("ext: readdir no node %s mode=%x\n",
               node != NULL && node->name != NULL ? node->name : "?",
               n != NULL ? n->mode : 0);
    }
    return 0;
  }
  if (*offset == 0 && ext_dbg_ok()) {
    log_info("ext: readdir %s ino=%u size=%x flags=%x mode=%x bs=%u ft=%d\n",
             node->name != NULL ? node->name : "?", n->ino, (u32)n->size,
             n->flags, n->mode, n->fs->block_size, n->fs->filetype);
    log_info("ext: extent block=%x len=%x start=%x%08x\n", n->iblock[3],
             n->iblock[4] & 0xffff, (n->iblock[4] >> 16) & 0xffff, n->iblock[5]);
  }
  start = *offset;
  byte = 0;
  index = 0;
  if (start != 0 && n->dir_index == start) {
    byte = n->dir_byte;
    index = start;
  }
  while (index < start) {
    char name[256];
    u32 ino;
    int rc = ext_dir_next(n, &byte, &ino, name, sizeof(name), NULL);
    if (rc != 1) {
      return 0;
    }
    if (ino == 0 || name[0] == 0) {
      continue;
    }
    index++;
  }
  while (read_count < count) {
    char name[256];
    u32 ino;
    u8 ft = 0;
    u32 nlen;
    int rc = ext_dir_next(n, &byte, &ino, name, sizeof(name), &ft);
    if (rc != 1) {
      break;
    }
    if (ino == 0 || name[0] == 0) {
      continue;
    }
    dirent->ino = ino;
    dirent->offset = index + 1;
    dirent->type = ext_dtype(n->fs, ft, ino);
    kmemset(dirent->name, 0, sizeof(dirent->name));
    kstrcpy(dirent->name, name);
    nlen = kstrlen(name) + 1;
    dirent->length = (u16)((19 + nlen + 7) & ~7u);
    nbytes += dirent->length;
    dirent++;
    index++;
    read_count++;
  }
  n->dir_index = index;
  n->dir_byte = byte;
  *offset = index;
  return nbytes;
}

static size_t ext_op_ioctl(vnode_t *node, u32 cmd, void *args) {
  ext_node_t *n;
  struct stat *st;
  if (cmd != IOC_STAT || args == NULL) {
    return 0;
  }
  n = ext_ensure(node);
  if (n == NULL) {
    return 0;
  }
  st = args;
  kmemset(st, 0, sizeof(*st));
  st->st_ino = n->ino;
  st->st_mode = n->mode;
  st->st_size = (off_t)n->size;
  st->st_blksize = n->fs != NULL ? (int)n->fs->block_size : 512;
  return 0;
}

static voperator_t ext_op = {
    .read = ext_op_read,
    .write = ext_op_write,
    .open = ext_op_open,
    .close = ext_op_close,
    .find = ext_op_find,
    .readdir = ext_op_readdir,
    .ioctl = ext_op_ioctl,
};

static void ext_fs_free(ext_fs_t *fs) {
  if (fs == NULL) {
    return;
  }
  if (fs->blk != NULL) {
    kfree(fs->blk);
  }
  if (fs->map != NULL) {
    kfree(fs->map);
  }
  if (fs->ino != NULL) {
    kfree(fs->ino);
  }
  kfree(fs);
}

static vnode_t *ext_probe(device_t *dev) {
  u8 *sb;
  ext_fs_t *fs;
  ext_node_t *root;
  vnode_t *node;
  u32 log_bs;
  u32 rev;
  u16 magic;
  if (dev == NULL || dev->name == NULL) {
    return NULL;
  }
  sb = kmalloc(1024, KERNEL_TYPE);
  if (sb == NULL) {
    return NULL;
  }
  if (ext_dev_read(dev, 1024, sb, 1024) != 0) {
    kfree(sb);
    return NULL;
  }
  magic = r16(sb + 0x38);
  if (magic != EXT_MAGIC) {
    kfree(sb);
    return NULL;
  }
  fs = kmalloc(sizeof(*fs), KERNEL_TYPE);
  if (fs == NULL) {
    kfree(sb);
    return NULL;
  }
  kmemset(fs, 0, sizeof(*fs));
  fs->dev = dev;
  fs->blk_lba = ~0ull;
  log_bs = r32(sb + 0x18);
  if (log_bs > 6) {
    kfree(sb);
    ext_fs_free(fs);
    return NULL;
  }
  fs->block_size = 1024u << log_bs;
  if (fs->block_size == 0 || fs->block_size > EXT_MAX_BLOCK) {
    kfree(sb);
    ext_fs_free(fs);
    return NULL;
  }
  rev = r32(sb + 0x4C);
  fs->inode_size = (rev == 0) ? 128 : r16(sb + 0x58);
  if (fs->inode_size < 128 || fs->inode_size > fs->block_size) {
    kfree(sb);
    ext_fs_free(fs);
    return NULL;
  }
  fs->inodes_per_group = r32(sb + 0x28);
  fs->blocks_per_group = r32(sb + 0x20);
  fs->first_data_block = r32(sb + 0x14);
  fs->incompat = r32(sb + 0x60);
  fs->filetype = (fs->incompat & EXT_INCOMPAT_FILETYPE) != 0;
  fs->blocks_count = r32(sb + 4);
  if (fs->incompat & EXT_INCOMPAT_64BIT) {
    fs->blocks_count |= (u64)r32(sb + 0x150) << 32;
    fs->desc_size = r16(sb + 0xFE);
    if (fs->desc_size < 64 || fs->desc_size > 64) {
      kfree(sb);
      ext_fs_free(fs);
      return NULL;
    }
  } else {
    fs->desc_size = 32;
  }
  if (fs->blocks_per_group == 0 || fs->inodes_per_group == 0) {
    kfree(sb);
    ext_fs_free(fs);
    return NULL;
  }
  fs->groups =
      (u32)((fs->blocks_count + fs->blocks_per_group - 1) / fs->blocks_per_group);
  fs->blk = kmalloc(fs->block_size, KERNEL_TYPE);
  fs->map = kmalloc(fs->block_size, KERNEL_TYPE);
  fs->ino = kmalloc(fs->inode_size, KERNEL_TYPE);
  if (fs->blk == NULL || fs->map == NULL || fs->ino == NULL) {
    kfree(sb);
    ext_fs_free(fs);
    return NULL;
  }
  if (fs->incompat & EXT_INCOMPAT_RECOVER) {
    log_warn("ext: %s journal needs recovery, read-only\n", dev->name);
  }
  log_info("ext: %s superblock bs=%u inode=%u\n", dev->name, fs->block_size,
           fs->inode_size);
  root = kmalloc(sizeof(*root), KERNEL_TYPE);
  if (root == NULL || ext_read_inode(fs, 2, root) != 0 || !S_ISDIR(root->mode)) {
    kfree(sb);
    if (root != NULL) {
      kfree(root);
    }
    ext_fs_free(fs);
    log_warn("ext: %s root inode unreadable\n", dev->name);
    return NULL;
  }
  kfree(sb);
  node = vfs_create_node((u8 *)dev->name, V_DIRECTORY);
  if (node == NULL) {
    kfree(root);
    ext_fs_free(fs);
    return NULL;
  }
  node->data = root;
  node->op = &ext_op;
  node->device = dev;
  return node;
}

static int ext_split_parent(const char *path, char *parent, char *leaf) {
  int i;
  int slash = -1;
  int len;
  if (path == NULL || path[0] != '/') {
    return -1;
  }
  len = (int)kstrlen(path);
  if (len < 2 || len >= 96) {
    return -1;
  }
  for (i = 0; i < len; i++) {
    if (path[i] == '/') {
      slash = i;
    }
  }
  if (slash <= 0) {
    parent[0] = '/';
    parent[1] = 0;
    kmemset(leaf, 0, 32);
    kstrncpy(leaf, path + 1, 31);
    return 0;
  }
  kmemset(parent, 0, 96);
  kstrncpy(parent, path, (size_t)slash);
  parent[slash] = 0;
  if (path[slash + 1] == 0) {
    return -1;
  }
  kmemset(leaf, 0, 32);
  kstrncpy(leaf, path + slash + 1, 31);
  return 0;
}

static vnode_t *ext_ensure_dir(const char *path) {
  vnode_t *found;
  vnode_t *dir;
  char parent[96];
  char leaf[32];
  if (path == NULL) {
    return NULL;
  }
  if (path[0] == '/' && path[1] == 0) {
    return vfs_find(NULL, "/");
  }
  found = vfs_find(NULL, (u8 *)path);
  if (found != NULL) {
    return found;
  }
  if (ext_split_parent(path, parent, leaf) != 0) {
    return NULL;
  }
  if (ext_ensure_dir(parent) == NULL) {
    return NULL;
  }
  dir = vfs_create_node((u8 *)leaf, V_DIRECTORY);
  vfs_mount(NULL, (u8 *)parent, dir);
  return vfs_find(NULL, (u8 *)path);
}

static int ext_mount_at(int index, const char *path) {
  vnode_t *exist;
  vnode_t *mp;
  char parent[96];
  char leaf[32];
  if (index < 0 || index >= ext_part_count || ext_parts[index].root == NULL ||
      ext_parts[index].mounted) {
    return -1;
  }
  if (path == NULL || path[0] != '/' || path[1] == 0) {
    return -1;
  }
  if (kstrncmp(path, "/dev", 4) == 0 && (path[4] == 0 || path[4] == '/')) {
    return -1;
  }
  exist = vfs_find(NULL, (u8 *)path);
  if (exist != NULL) {
    if ((exist->flags & V_DIRECTORY) == 0) {
      log_warn("ext: %s is not a directory\n", path);
      return -1;
    }
    exist->super = ext_parts[index].root;
    ext_parts[index].mounted = 1;
    log_info("ext: mount %s on %s (read-only)\n", ext_parts[index].name, path);
    return 0;
  }
  if (ext_split_parent(path, parent, leaf) != 0) {
    return -1;
  }
  if (ext_ensure_dir(parent) == NULL) {
    return -1;
  }
  mp = vfs_create_node((u8 *)leaf, V_DIRECTORY);
  if (mp == NULL) {
    return -1;
  }
  mp->super = ext_parts[index].root;
  vfs_mount(NULL, (u8 *)parent, mp);
  ext_parts[index].mounted = 1;
  log_info("ext: mount %s on %s (read-only)\n", ext_parts[index].name, path);
  return 0;
}

static int ext_mount_cb(const char *key, const char *val, void *user) {
  int i;
  (void)user;
  if (key == NULL || val == NULL || val[0] == 0 || kstrcmp(key, "sda") == 0 ||
      kstrcmp(key, "sdb") == 0) {
    return 1;
  }
  if (ext_part_count > 0 && ext_parts[0].mounted &&
      kstrcmp(key, ext_parts[0].name) == 0) {
    return 1;
  }
  for (i = 0; i < ext_part_count; i++) {
    if (ext_parts[i].root != NULL && kstrcmp(ext_parts[i].name, key) == 0) {
      if (!ext_parts[i].mounted && ext_mount_at(i, val) != 0) {
        log_warn("ext: mount %s on %s failed\n", key, val);
      }
      return 1;
    }
  }
  return 1;
}

static void ext_mount_apply(void) {
  int i;
  int n;
  n = sysconf_foreach("mount", ext_mount_cb, NULL);
  if (n > 0) {
    return;
  }
  if (ext_ensure_dir("/mnt") == NULL) {
    log_warn("ext: cannot create /mnt\n");
    return;
  }
  for (i = 1; i < ext_part_count; i++) {
    char path[16];
    if (ext_parts[i].root == NULL || ext_parts[i].mounted) {
      continue;
    }
    kstrcpy(path, "/mnt/");
    kstrcat(path, ext_parts[i].name);
    if (ext_mount_at(i, path) != 0) {
      log_warn("ext: auto mount %s failed\n", ext_parts[i].name);
    }
  }
}

static void ext_init(void) {
  int i;
  int n = 0;
  log_info("ext init\n");
  for (i = 0; i < 4; i++) {
    device_t *dev = device_find(DEVICE_SATA0 + i);
    if (dev == NULL) {
      break;
    }
    kmemset(&ext_parts[n], 0, sizeof(ext_parts[n]));
    kstrncpy(ext_parts[n].name, dev->name != NULL ? dev->name : "sd", 7);
    ext_parts[n].dev = dev;
    ext_parts[n].root = ext_probe(dev);
    n++;
  }
  ext_part_count = n;
  if (n > 0 && ext_parts[0].root != NULL) {
    vnode_t *root = vfs_find(NULL, "/");
    if (root != NULL && root->super == NULL) {
      root->super = ext_parts[0].root;
      ext_parts[0].mounted = 1;
      log_info("ext: mount %s on / (read-only)\n", ext_parts[0].name);
    }
  }
  sysconf_on_ready(ext_mount_apply);
}

static void ext_exit(void) { log_info("ext exit\n"); }

module_t ext_module = {.name = "ext", .init = ext_init, .exit = ext_exit};
