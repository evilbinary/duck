/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 *
 * 读整卡（DEVICE_SATA），按 MBR / GPT / 超级软盘切出最多 4 个分区。
 * 第 0 张卡是 sda，分区 sda1、sda2…；第 1 张卡是 sdb、sdb1、sdb2。
 * /dev/sda 的读只返回这段分区表文字。分区设备的扇区 0 是它自己的 VBR。
 ********************************************************************/
#include "kernel/device.h"
#include "kernel/string.h"
#include "kernel/vfs.h"

#if defined(ARM) || defined(ARM64) || defined(__aarch64__)
#define PART_IOC_MAGIC 's'
#else
#define PART_IOC_MAGIC 'a'
#endif
#define PART_IOC_READ_OFFSET _IOW(PART_IOC_MAGIC, 3, int)
#define PART_IOC_WRITE_OFFSET _IOW(PART_IOC_MAGIC, 4, int)
#define PART_IOC_WRITE_OFFSET64 _IOW(PART_IOC_MAGIC, 5, long)

#define PART_MAX 4
#define PART_SS 512

typedef struct part_priv {
  device_t *disk;
  u32 start;
  u32 count;
  u64 pos;
} part_priv_t;

static part_priv_t privs[PART_MAX];
static char names[PART_MAX][8];
static int part_count;
static int part_skip_logged;

/* 一张卡一份。name 是整卡节点（sda），text 是 cat /dev/sda 读到的分区表。 */
#define DISK_MAX 2
typedef struct part_disk {
  char name[8];
  char text[256];
  u32 text_len;
  int parts;
  device_t *dev;
} part_disk_t;

static part_disk_t disks[DISK_MAX];

static u16 ld16(const u8 *p) { return (u16)p[0] | ((u16)p[1] << 8); }

static u32 ld32(const u8 *p) {
  return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u64 ld64(const u8 *p) { return (u64)ld32(p) | ((u64)ld32(p + 4) << 32); }

static int disk_read_lba(device_t *disk, u32 lba, void *buf) {
  u64 off = (u64)lba * PART_SS;
  if (disk == NULL || disk->read == NULL || off > 0xffffffffull) {
    return -1;
  }
  if (disk->ioctl != NULL) {
    disk->ioctl(disk, PART_IOC_WRITE_OFFSET, (u32)off);
  }
  if (disk->read(disk, buf, PART_SS) != PART_SS) {
    return -1;
  }
  return 0;
}

static int part_abs(part_priv_t *p, size_t len, u64 *off) {
  u64 base;
  u64 end;
  if (p == NULL || p->disk == NULL || len == 0 || off == NULL) {
    return -1;
  }
  if (p->count != 0) {
    u64 maxb = (u64)p->count * PART_SS;
    if ((u64)p->pos >= maxb || (u64)len > maxb - (u64)p->pos) {
      return -1;
    }
  }
  base = (u64)p->start * PART_SS + (u64)p->pos;
  end = base + (u64)len;
  if (end < base || (base / PART_SS) > 0xffffffffull ||
      ((end - 1) / PART_SS) > 0xffffffffull) {
    return -1;
  }
  *off = base;
  return 0;
}

static size_t part_read(device_t *dev, void *buf, size_t len) {
  part_priv_t *p = dev->data;
  u64 off;
  if (p == NULL || p->disk == NULL || p->disk->read == NULL || buf == NULL) {
    return 0;
  }
  if (part_abs(p, len, &off) != 0) {
    return 0;
  }
  if (p->disk->ioctl != NULL) {
    p->disk->ioctl(p->disk, PART_IOC_WRITE_OFFSET64, off);
  }
  return p->disk->read(p->disk, buf, len);
}

static size_t part_write(device_t *dev, const void *buf, size_t len) {
  part_priv_t *p = dev->data;
  u64 off;
  if (p == NULL || p->disk == NULL || p->disk->write == NULL || buf == NULL) {
    return 0;
  }
  if (part_abs(p, len, &off) != 0) {
    return 0;
  }
  if (p->disk->ioctl != NULL) {
    p->disk->ioctl(p->disk, PART_IOC_WRITE_OFFSET64, off);
  }
  return p->disk->write(p->disk, buf, len);
}

static size_t part_ioctl(device_t *dev, u32 cmd, ...) {
  part_priv_t *p = dev->data;
  va_list ap;
  if (p == NULL) {
    return 0;
  }
  va_start(ap, cmd);
  if (cmd == PART_IOC_WRITE_OFFSET64) {
    p->pos = va_arg(ap, u64);
    va_end(ap);
    return 0;
  }
  {
    uint offset = va_arg(ap, uint);
    va_end(ap);
    if (cmd == PART_IOC_WRITE_OFFSET) {
      p->pos = offset;
      return 0;
    }
    if (cmd == PART_IOC_READ_OFFSET) {
      return (size_t)p->pos;
    }
  }
  return 0;
}

static size_t part_stat(device_t *dev, dstat_t *st) {
  part_priv_t *p = dev->data;
  u64 bytes;
  if (st == NULL || p == NULL) {
    return 0;
  }
  bytes = (u64)p->count * PART_SS;
  st->is_dir = 0;
  st->size = (bytes > (u64)(size_t)-1) ? 0 : (size_t)bytes;
  return 0;
}

static void text_add(part_disk_t *d, const char *name, int type, u32 start,
                     u32 count) {
  char line[80];
  int n;
  int room;
  if (d == NULL || name == NULL) {
    return;
  }
  if (type >= 0) {
    n = ksnprintf(line, sizeof(line), "%s type=%x start=%x count=%x\n", name,
                  type, start, count);
  } else {
    n = ksnprintf(line, sizeof(line), "%s start=%x count=%x\n", name, start,
                  count);
  }
  if (n <= 0) {
    return;
  }
  room = (int)sizeof(d->text) - 1 - (int)d->text_len;
  if (n > room) {
    n = room;
  }
  if (n <= 0) {
    return;
  }
  kmemcpy(d->text + d->text_len, line, (size_t)n);
  d->text_len += (u32)n;
  d->text[d->text_len] = 0;
}

static void part_add(device_t *disk, int di, u32 start, u32 count, int type) {
  device_t *dev;
  part_disk_t *d;
  int n;
  if (di < 0 || di >= DISK_MAX) {
    return;
  }
  d = &disks[di];
  if (part_count >= PART_MAX) {
    if (!part_skip_logged) {
      log_info("partition: more than %d, skip rest\n", PART_MAX);
      part_skip_logged = 1;
    }
    return;
  }
  if ((u64)start * PART_SS > 0xffffffffull) {
    log_info("partition: skip start=%x (offset overflow)\n", start);
    return;
  }
  n = d->parts;
  if (n >= 9) {
    return;
  }
  names[part_count][0] = 's';
  names[part_count][1] = 'd';
  names[part_count][2] = d->name[2];
  names[part_count][3] = (char)('1' + n);
  names[part_count][4] = 0;
  privs[part_count].disk = disk;
  privs[part_count].start = start;
  privs[part_count].count = count;
  privs[part_count].pos = 0;
  dev = device_create((u8 *)names[part_count], DEVICE_SATA0 + part_count,
                      DEVICE_TYPE_BLOCK);
  if (dev == NULL) {
    return;
  }
  dev->read = part_read;
  dev->write = part_write;
  dev->ioctl = part_ioctl;
  dev->stat = part_stat;
  dev->data = &privs[part_count];
  device_add(dev);
  text_add(d, names[part_count], type, start, count);
  log_info("partition: %s start=%x count=%x\n", names[part_count], start, count);
  d->parts++;
  part_count++;
}

static int looks_like_fat(const u8 *b) {
  if (b[510] != 0x55 || b[511] != 0xAA) {
    return 0;
  }
  if (!(b[0] == 0xE9 || (b[0] == 0xEB && b[2] == 0x90))) {
    return 0;
  }
  if (b[54] == 'F' && b[55] == 'A' && b[56] == 'T') {
    return 1;
  }
  if (b[82] == 'F' && b[83] == 'A' && b[84] == 'T') {
    return 1;
  }
  return 0;
}

static int mbr_has_part(const u8 *sec) {
  int i;
  for (i = 0; i < 4; i++) {
    const u8 *e = sec + 446 + i * 16;
    u8 type = e[4];
    u32 start = ld32(e + 8);
    u32 count = ld32(e + 12);
    if (type == 0 || type == 0x05 || type == 0x0F || start == 0 || count == 0) {
      continue;
    }
    return 1;
  }
  return 0;
}

static void scan_mbr(device_t *disk, int di, const u8 *sec) {
  int i;
  log_info("partition: mbr\n");
  for (i = 0; i < 4; i++) {
    const u8 *e = sec + 446 + i * 16;
    u8 type = e[4];
    u32 start = ld32(e + 8);
    u32 count = ld32(e + 12);
    if (type == 0 || type == 0x05 || type == 0x0F || start == 0 || count == 0) {
      continue;
    }
    part_add(disk, di, start, count, type);
  }
}

static void scan_gpt(device_t *disk, int di, u8 *sec) {
  u64 pt_lba;
  u32 pt_num;
  u32 pt_esz;
  u32 lba;
  u32 i;
  int ent;
  int per;
  if (disk_read_lba(disk, 1, sec) != 0) {
    log_info("partition: gpt header unreadable\n");
    return;
  }
  if (kmemcmp(sec, "EFI PART", 8) != 0) {
    log_info("partition: bad gpt signature\n");
    return;
  }
  pt_lba = ld64(sec + 72);
  pt_num = ld32(sec + 80);
  pt_esz = ld32(sec + 84);
  log_info("partition: gpt\n");
  if (pt_lba > 0xffffffffull || pt_esz < 128 || pt_esz > PART_SS) {
    return;
  }
  per = PART_SS / (int)pt_esz;
  if (per <= 0) {
    return;
  }
  lba = (u32)pt_lba;
  ent = 0;
  for (i = 0; i < pt_num; i++) {
    u8 *e;
    u64 first;
    u64 last;
    int z;
    int nonzero = 0;
    if (part_count >= PART_MAX) {
      if (!part_skip_logged) {
        log_info("partition: more than %d, skip rest\n", PART_MAX);
        part_skip_logged = 1;
      }
      return;
    }
    if (ent == 0) {
      if (disk_read_lba(disk, lba, sec) != 0) {
        return;
      }
      lba++;
    }
    if ((u32)ent * pt_esz + 48 > PART_SS) {
      ent++;
      if (ent >= per) {
        ent = 0;
      }
      continue;
    }
    e = sec + ent * (int)pt_esz;
    ent++;
    if (ent >= per) {
      ent = 0;
    }
    for (z = 0; z < 16; z++) {
      if (e[z] != 0) {
        nonzero = 1;
        break;
      }
    }
    if (!nonzero) {
      continue;
    }
    first = ld64(e + 32);
    last = ld64(e + 40);
    if (first > 0xffffffffull || last > 0xffffffffull || last < first) {
      log_info("partition: skip gpt entry lba out of 32-bit\n");
      continue;
    }
    {
      u64 cnt = last - first + 1;
      if (cnt > 0xffffffffull) {
        continue;
      }
      part_add(disk, di, (u32)first, (u32)cnt, -1);
    }
  }
}

static u32 part_info_read(vnode_t *node, u32 offset, u32 nbytes, u8 *buf) {
  part_disk_t *d;
  u32 left;
  if (node == NULL || buf == NULL || nbytes == 0) {
    return 0;
  }
  d = node->data;
  if (d == NULL || offset >= d->text_len) {
    return 0;
  }
  left = d->text_len - offset;
  if (nbytes > left) {
    nbytes = left;
  }
  kmemcpy(buf, d->text + offset, nbytes);
  return nbytes;
}

static voperator_t part_info_op = {
    .read = part_info_read,
};

static void publish_disk(int di) {
  vnode_t *node;
  part_disk_t *d = &disks[di];
  if (d->name[0] == 0) {
    return;
  }
  node = vfs_create_node((u8 *)d->name, V_FILE | V_BLOCKDEVICE);
  if (node == NULL) {
    return;
  }
  node->op = &part_info_op;
  node->data = d;
  node->device = d->dev;
  node->length = d->text_len;
  vfs_mount(NULL, (u8 *)"/dev", node);
  log_info("partition: %s\n", d->name);
}

/* 第 di 张卡。0 → sda/sda1/sda2，1 → sdb/sdb1/sdb2。 */
static void scan_one(device_t *disk, int di) {
  u8 *sec;
  u32 count;
  int before;
  part_disk_t *d;
  if (disk == NULL || disk->read == NULL || di < 0 || di >= DISK_MAX) {
    return;
  }
  d = &disks[di];
  kmemset(d, 0, sizeof(*d));
  d->dev = disk;
  d->name[0] = 's';
  d->name[1] = 'd';
  d->name[2] = (char)('a' + di);
  d->name[3] = 0;
  sec = kmalloc(PART_SS, KERNEL_TYPE);
  if (sec == NULL) {
    return;
  }
  before = part_count;
  if (disk_read_lba(disk, 0, sec) != 0) {
    log_info("partition: cannot read sector 0\n");
    kfree(sec);
    return;
  }
  if (sec[510] == 0x55 && sec[511] == 0xAA && sec[446 + 4] == 0xEE) {
    scan_gpt(disk, di, sec);
  } else if (sec[510] == 0x55 && sec[511] == 0xAA && mbr_has_part(sec)) {
    /* 有有效分区项就是 MBR。FAT 引导扇区的分区表是空的，不会走到这里。 */
    scan_mbr(disk, di, sec);
  } else if (looks_like_fat(sec)) {
    count = ld16(sec + 19);
    if (count == 0) {
      count = ld32(sec + 32);
    }
    log_info("partition: whole disk\n");
    part_add(disk, di, 0, count, -1);
  } else {
    log_info("partition: whole disk\n");
    part_add(disk, di, 0, 0, -1);
  }
  if (part_count == before) {
    log_info("partition: whole disk\n");
    part_add(disk, di, 0, 0, -1);
  }
  kfree(sec);
  publish_disk(di);
}

static void partition_init(void) {
  device_t *disk;
  /* 现在只有一张卡（DEVICE_SATA）→ sda。下一张卡接在这里，di=1 就是 sdb。 */
  disk = device_find(DEVICE_SATA);
  scan_one(disk, 0);
}

static void partition_exit(void) { log_info("partition exit\n"); }

module_t partition_module = {
    .name = "partition", .init = partition_init, .exit = partition_exit};
