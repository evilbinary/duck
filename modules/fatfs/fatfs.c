#include "diskio.h"
#include "ff.h"
#include "kernel/device.h"
#include "kernel/fd.h"
#include "kernel/memory.h"
#include "kernel/stat.h"
#include "posix/sysfn.h"
#include "rtc/rtc.h"
#include "modules/sysconf/sysconf.h"

// Select the block-device IOCTL encoding that matches the active block driver.
// ARM32/ARM64 use SDHCI (magic 's'); non-ARM platforms typically use AHCI (magic 'a').
#if defined(ARM) || defined(ARM64) || defined(__aarch64__)
#include "mmc/sdhci.h"
#else
#include "ahci/ahci.h"
#endif

#define MAX_FILE_PATH 256

void fat_init_op(vnode_t *node);

enum {
  DT_UNKNOWN = 0,
#define DT_UNKNOWN DT_UNKNOWN
  DT_FIFO = 1,
#define DT_FIFO DT_FIFO
  DT_CHR = 2,
#define DT_CHR DT_CHR
  DT_DIR = 4,
#define DT_DIR DT_DIR
  DT_BLK = 6,
#define DT_BLK DT_BLK
  DT_REG = 8,
#define DT_REG DT_REG
  DT_LNK = 10,
#define DT_LNK DT_LNK
  DT_SOCK = 12,
#define DT_SOCK DT_SOCK
  DT_WHT = 14
#define DT_WHT DT_WHT
};
typedef struct file_info {
  FATFS fs;
  FIL fil;
  DIR dir;
  FILINFO file;
  int offset;
  /* 【目录列举位置】= 已从 dir 里读出的条目数（0-based 下一条的下标）。
   * 连续 readdir（上层 getdents64 一次取一条）时靠它判断"可以接着读"，
   * 避免每次都 close+open+从头重扫（见 fat_op_read_dir 的性能注释）。 */
  int dir_pos;
  char fat_path[MAX_FILE_PATH];
  /* FatFs 卷号，例如 "0:"。多分区时每个节点自己的卷，不再共用全局 "1:"。 */
  char vol[4];
} file_info_t;

vnode_t *default_node = NULL;

/* FatFs 卷号对应的块设备。分区偏移由 partition 模块的设备自己加。 */
static device_t *fat_pdrv_dev[4];

typedef struct fat_vol {
  vnode_t *node;
  char name[8];
  int mounted;
} fat_vol_t;

static fat_vol_t fat_vols[4];
static int fat_vol_count;

extern vnode_t *devfs_create_device(device_t *dev);

static int fat_join_path(const char *parent_path, const char *name, char *out,
                         size_t outsz) {
  if (out == NULL || outsz == 0 || name == NULL || name[0] == '\0') {
    return -1;
  }
  if (parent_path == NULL || parent_path[0] == '\0') {
    parent_path = "/";
  }
  size_t plen = kstrlen(parent_path);
  size_t nlen = kstrlen(name);
  int need_slash =
      (plen > 1 || parent_path[0] != '/') && parent_path[plen - 1] != '/';
  size_t total = plen + (need_slash ? 1 : 0) + nlen + 1;
  if (total > outsz) {
    return -1;
  }
  kstrcpy(out, parent_path);
  plen = kstrlen(out);
  if (need_slash) {
    out[plen++] = '/';
    out[plen] = '\0';
  }
  kstrcpy(out + plen, name);
  return 0;
}

static const char *fat_vol_prefix(const file_info_t *file_info) {
  if (file_info != NULL && file_info->vol[1] == ':') {
    return file_info->vol;
  }
  if (default_node != NULL && default_node->data != NULL) {
    file_info_t *d = default_node->data;
    if (d->vol[1] == ':') {
      return d->vol;
    }
  }
  return "0:";
}

static void fat_copy_vol(file_info_t *dst, const file_info_t *src) {
  const char *vol;
  if (dst == NULL) {
    return;
  }
  vol = fat_vol_prefix(src);
  dst->vol[0] = vol[0];
  dst->vol[1] = ':';
  dst->vol[2] = '\0';
}

static int fat_volume_path(const file_info_t *file_info, char *buf) {
  const char *vol;
  if (file_info == NULL || file_info->fat_path[0] == '\0') {
    return -1;
  }
  vol = fat_vol_prefix(file_info);
  kstrcpy(buf, vol);
  kstrcpy(buf + 2, file_info->fat_path);
  return 0;
}

static vnode_t *fat_super_node(vnode_t *node) {
  if (node == NULL) {
    return default_node;
  }
  if ((node->flags & V_BLOCKDEVICE) == V_BLOCKDEVICE) {
    return node;
  }
  if (node->super != NULL) {
    return node->super;
  }
  return default_node;
}

static void fat_init_file_info_from_node(vnode_t *node, file_info_t *file_info,
                                         file_info_t *super_file_info) {
  if (file_info == NULL || super_file_info == NULL) {
    return;
  }
  file_info->fs = super_file_info->fs;
  fat_copy_vol(file_info, super_file_info);
  if (file_info->fat_path[0] != '\0') {
    return;
  }
  /* 挂载点（/ 或 /mnt/sdb）本身就是该卷的根。 */
  if (node->super != NULL && super_file_info->fat_path[0] != '\0') {
    kstrcpy(file_info->fat_path, super_file_info->fat_path);
  }
}

int fat_node_path(vnode_t *node, char *buf, size_t bufsz) {
  if (node == NULL || buf == NULL || bufsz == 0) {
    return -1;
  }
  file_info_t *file_info = node->data;
  if (file_info == NULL || file_info->fat_path[0] == '\0') {
    return -1;
  }
  if (kstrlen(file_info->fat_path) + 1 >= bufsz) {
    return -1;
  }
  kstrcpy(buf, file_info->fat_path);
  return 0;
}

static int fat_reopen_file(vnode_t *node) {
  if (node == NULL || (node->flags & V_DIRECTORY) == V_DIRECTORY) {
    return -1;
  }

  file_info_t *file_info = node->data;
  if (file_info == NULL) {
    return -1;
  }

  char buf[MAX_FILE_PATH];
  if (fat_volume_path(file_info, buf) < 0) {
    return -1;
  }

  f_close(&file_info->fil);
  kmemset(&file_info->fil, 0, sizeof(FIL));

  int res = f_open(&file_info->fil, buf, FA_READ | FA_WRITE);
  if (res != FR_OK) {
    log_error("fat reopen file %s path %s error code %d\n", node->name, buf, res);
    return -1;
  }
  return 0;
}

static uint fat_device_read(vnode_t *node, uint offset, size_t nbytes,
                           u8 *buffer) {
  uint ret = 0;
  device_t *dev = (device_t *)node->device;
  if (dev == NULL || dev->read == NULL) {
    return ret;
  }
  if (dev->ioctl != NULL) {
    dev->ioctl(dev, IOC_WRITE_OFFSET, offset);
  }
  ret = dev->read(dev, buffer, nbytes);
  return ret;
}

static uint fat_device_write(vnode_t *node, uint offset, size_t nbytes,
                            u8 *buffer) {
  uint ret = 0;
  device_t *dev = (device_t *)node->device;
  if (dev == NULL || dev->write == NULL) {
    return ret;
  }
  if (dev->ioctl != NULL) {
    dev->ioctl(dev, IOC_WRITE_OFFSET, offset);
  }
  ret = dev->write(dev, buffer, nbytes);
  return ret;
}

int MMC_disk_initialize() {
  log_debug("MMC_disk_initialize\n");
  return RES_OK;
}

int MMC_disk_status() { return RES_OK; }

int MMC_disk_read(BYTE pdrv, char *buffer, LBA_t sector, int count) {
  device_t *dev;
  uint offset;
  uint length;
  uint ret;

  dev = (pdrv < 4) ? fat_pdrv_dev[pdrv] : NULL;
  if (dev == NULL || dev->read == NULL) {
    log_error("MMC_disk_read: bad drive %d\n", pdrv);
    return RES_ERROR;
  }
  if ((u64)sector * FF_MIN_SS > 0xffffffffull) {
    return RES_PARERR;
  }
  offset = (uint)sector * FF_MIN_SS;
  length = (uint)count * FF_MIN_SS;
  if (dev->ioctl != NULL) {
    dev->ioctl(dev, IOC_WRITE_OFFSET, offset);
  }
  ret = dev->read(dev, buffer, length);
  if (ret != length) {
    log_error("MMC_disk_read: read failed at sector %d, expected %d bytes, got %d\n",
              (u32)sector, length, ret);
    return RES_ERROR;
  }
  return RES_OK;
}

int MMC_disk_write(BYTE pdrv, const char *buffer, LBA_t sector, int count) {
  device_t *dev;
  uint offset;
  uint length;
  uint ret;

  if (count < 0) {
    return RES_PARERR;
  }
  dev = (pdrv < 4) ? fat_pdrv_dev[pdrv] : NULL;
  if (dev == NULL || dev->write == NULL) {
    log_error("MMC_disk_write: bad drive %d\n", pdrv);
    return RES_ERROR;
  }
  if ((u64)sector * FF_MIN_SS > 0xffffffffull) {
    return RES_PARERR;
  }
  offset = (uint)sector * FF_MIN_SS;
  length = (uint)count * FF_MIN_SS;
  if (dev->ioctl != NULL) {
    dev->ioctl(dev, IOC_WRITE_OFFSET, offset);
  }
  ret = dev->write(dev, (void *)buffer, length);
  if (ret != length) {
    log_error("MMC_disk_write: write failed at sector %d, expected %d bytes, got %d\n",
              (u32)sector, length, ret);
    return RES_ERROR;
  }
  return RES_OK;
}

int MMC_disk_ioctl(u8 pdrv, u8 cmd, void *buff) {
  // log_debug("MMC_disk_ioctl cmd %d buff %x\n",cmd,buff);
  DWORD *pdword = NULL;
  WORD *pword = NULL;
  switch (cmd) {
    case GET_SECTOR_COUNT: {
      dstat_t st;
      pdword = (DWORD *)buff;
      *pdword = 9999999 + 1;
      if (pdrv < 4 && fat_pdrv_dev[pdrv] != NULL &&
          fat_pdrv_dev[pdrv]->stat != NULL) {
        kmemset(&st, 0, sizeof(st));
        fat_pdrv_dev[pdrv]->stat(fat_pdrv_dev[pdrv], &st);
        if (st.size >= FF_MIN_SS) {
          *pdword = (DWORD)(st.size / FF_MIN_SS);
        }
      }
      return RES_OK;
    }

    case GET_SECTOR_SIZE:
      pword = (WORD *)buff;
      *pword = FF_MIN_SS;
      return RES_OK;

    case GET_BLOCK_SIZE:
      pdword = (DWORD *)buff;
      *pdword = 1;
      return RES_OK;
    case CTRL_SYNC:
      return RES_OK;
    case CTRL_TRIM:
      return RES_PARERR;
    default:
      break;
  }
  /* 未知命令必须报参数错误；旧代码回 RES_OK，FatFs 会以为设备支持
   * 该命令并继续用未填充的 buff。 */
  return RES_PARERR;
}

uint get_fattime(void) {
  rtc_time_t time;
  vnode_t *time_node;
  int year;

  kmemset(&time, 0, sizeof(time));
  time.day = 1;
  time.month = 1;
  time.year = 1980;

  /* 直接 vread，不要再 sys_open_kernel：那条路径每次调用都会占一个 fd 且
   * 从没 sys_close（f_open/f_write 都会走 get_fattime ⇒ fd 表被慢慢耗尽）。 */
  time_node = vfs_find(NULL, "/dev/time");
  if (time_node != NULL) {
    if (vread(time_node, 0, sizeof(rtc_time_t), (u8 *)&time) == 0) {
      log_error("erro read time\n");
      return 0;
    }
  }

  /* rtc_time_t 的 month 是 1-12（rtc_get_time → seconds_to_date），
   * 不能再 +1，否则 12 月会溢出成 13。年域 5 bit 只能放 1980..2107。 */
  year = (int)time.year;
  if (year < 1980) {
    year = 1980;
  } else if (year > 2107) {
    year = 2107;
  }
  return (uint)(year - 1980) << 25 | (uint)(time.month & 0x0f) << 21 |
         (uint)(time.day & 0x1f) << 16 | (uint)(time.hour & 0x1f) << 11 |
         (uint)(time.minute & 0x3f) << 5 | (uint)(time.second >> 1 & 0x1f);
}

static void print_hex(u8 *addr, uint size) {
  for (int x = 0; x < size; x++) {
    kprintf("%02x ", addr[x]);
    if (x != 0 && (x % 32) == 0) {
      kprintf("\n");
    }
  }
  kprintf("\n\r");
}

uint fat_op_read(vnode_t *node, uint offset, size_t nbytes, u8 *buffer) {
  file_info_t *file_info = node->data;
  if (file_info == NULL) {
    log_error("fat read %s file_info is null\n", node != NULL ? node->name : "<null>");
    return -1;
  }

  /* 顺序读时 fptr 已在 offset，跳过 f_lseek（省一次 FAT 窗口访问） */
  if (offset != (uint)file_info->fil.fptr) {
    int seek_res = f_lseek(&file_info->fil, offset);
    if (seek_res == FR_INVALID_OBJECT) {
      if (fat_reopen_file(node) < 0) {
        return -1;
      }
      seek_res = f_lseek(&file_info->fil, offset);
    }
    if (seek_res != FR_OK) {
      log_error("fat seek %s error code %d offset %d\n", node->name, seek_res, offset);
      return -1;
    }
  }
  // kprintf("read file-->%s fil: %x offset %d\n", node->name, &file_info->fil,
  //         offset);
  
  //FFOBJID* obj=&file_info->fil.obj;
  //kprintf("read =>%x %x type %x %x %x\n",obj, obj->fs , obj->fs->fs_type , obj->id , obj->fs->id);

  int readbytes = 0;
  int res = f_read(&file_info->fil, buffer, nbytes, &readbytes);
  if (res == FR_INVALID_OBJECT) {
    if (fat_reopen_file(node) < 0) {
      return -1;
    }
    if (offset >= 0) {
      int seek_res = f_lseek(&file_info->fil, offset);
      if (seek_res != FR_OK) {
        log_error("fat seek retry %s error code %d offset %d\n", node->name, seek_res,
                  offset);
        return -1;
      }
    }
    res = f_read(&file_info->fil, buffer, nbytes, &readbytes);
  }
  if (res != FR_OK) {
    log_error("fat read %s error code %d\n", node->name, res);
    return -1;
  }

  // log_debug("fat_op_read offset %x nbytes %d readbytes %d ret=%d\n", offset,
            // nbytes, readbytes, res);
  // print_hex(buffer,readbytes);

  return readbytes;
}

uint fat_op_write(vnode_t *node, uint offset, size_t nbytes, u8 *buffer) {
  file_info_t *file_info = node->data;
  if (file_info == NULL) {
    log_error("write file info faild not opend\n");
    return -1;
  }
  // log_debug("fat_op_write fil %x %d %s\n",&file_info->fil,nbytes,buffer);
  /* offset 是 uint，"offset >= 0" 恒真；要判的是 seek 本身是否成功。
   * 追加写(O_APPEND)的 offset 可能被上层传成 0 或旧长度，这里仍按
   * 显式 offset 定位，和 fat_op_read 保持一致（顺带省一次无谓 lseek）。 */
  if ((FSIZE_t)offset != file_info->fil.fptr) {
    int seek_res = f_lseek(&file_info->fil, offset);
    if (seek_res == FR_INVALID_OBJECT) {
      if (fat_reopen_file(node) < 0) {
        return -1;
      }
      seek_res = f_lseek(&file_info->fil, offset);
    }
    if (seek_res != FR_OK) {
      log_error("fat write seek %s error code %d offset %d\n", node->name,
                seek_res, offset);
      return -1;
    }
  }

  UINT written = 0;
  int res = f_write(&file_info->fil, buffer, nbytes, &written);
  if (res != FR_OK) {
    log_error("fat write %s error code %d\n", node->name, res);
    return -1;
  }
  /* 文件长度以 fptr（写完后位置）为准；mmap/stat 依赖 node->length。 */
  if ((u32)file_info->fil.fptr > node->length) {
    node->length = (u32)file_info->fil.fptr;
  }

  return written;
}

uint fat_op_open(vnode_t *node, uint mode) {
  char *name = node->name;
  file_info_t *file_info = node->data;
  char buf[MAX_FILE_PATH];

  if (file_info == NULL) {
    vnode_t *super_node = fat_super_node(node);
    if (super_node == NULL || super_node->data == NULL) {
      log_error("fat open %s missing super file_info\n",
                node->name != NULL ? node->name : "<null>");
      return -1;
    }
    file_info = kmalloc(sizeof(file_info_t), KERNEL_TYPE);
    kmemset(file_info, 0, sizeof(file_info_t));
    file_info_t *super_file_info = super_node->data;
    fat_init_file_info_from_node(node, file_info, super_file_info);
    node->data = file_info;
  }

  if (file_info == NULL) {
    log_error("fat may init faild\n");
    return -1;
  }

  file_info->offset = 0;

  if ((mode & O_CREAT) == O_CREAT) {
    log_debug("create new file %s\n", name);
    /* 【新建节点：自建 file_info + 拼全路径】
     * O_CREAT 新建出来的节点，data 往往直接共享父目录的 file_info，其
     * fat_path 是父目录路径（根就是 "/"）。拿它去 f_open 等于打开目录本身，
     * 得到 FR_INVALID_NAME(6) —— 之前 mmap 文件测试就是卡在这里。
     * 这里做两件事：
     *   1) 若和父目录共享，换成一自己的副本（绝不能改写父目录的 fat_path）；
     *   2) 路径 = 父 fat_path + "/" + 节点名。
     * O_TRUNC 对应 FA_CREATE_ALWAYS（fopen("w") 就是 O_CREAT|O_TRUNC）。 */
    vnode_t *super_node = fat_super_node(node);
    if (super_node == NULL || super_node->data == NULL) {
      log_error("create %s: missing parent file_info\n",
                name != NULL ? name : "<null>");
      return -1;
    }
    file_info_t *parent_info = super_node->data;
    if (file_info == parent_info) {
      file_info_t *own = kmalloc(sizeof(file_info_t), KERNEL_TYPE);
      if (own == NULL) {
        return -1;
      }
      kmemset(own, 0, sizeof(file_info_t));
      own->fs = parent_info->fs;
      fat_copy_vol(own, parent_info);
      file_info = own;
      node->data = file_info;
    }
    if (fat_join_path(parent_info->fat_path, name, file_info->fat_path,
                      sizeof(file_info->fat_path)) < 0) {
      log_error("create %s: bad path (parent=%s)\n",
                name != NULL ? name : "<null>", parent_info->fat_path);
      return -1;
    }
    if (fat_volume_path(file_info, buf) < 0) {
      return -1;
    }
    BYTE create_mode = FA_READ | FA_WRITE | ((mode & O_TRUNC) != 0
                                                 ? FA_CREATE_ALWAYS
                                                 : FA_OPEN_ALWAYS);
    int res = f_open(&file_info->fil, buf, create_mode);
    if (res != FR_OK) {
      log_error("open create file %s path %s error code %d\n", node->name, buf,
                res);
      return -1;
    }
    node->length = (u32)f_size(&file_info->fil);

  } else if ((mode & O_DIRECTORY) == O_DIRECTORY ||
             (node->flags & V_DIRECTORY) == V_DIRECTORY) {
    if (fat_volume_path(file_info, buf) < 0) {
      return -1;
    }
    int res = f_opendir(&file_info->dir, buf);
    if (res != FR_OK) {
      log_error("open dir %s error code %d\n", node->name, res);
      return -1;
    }
    file_info->dir_pos = 0; /* 新开的 DIR 从位置 0 开始（见 fat_op_read_dir） */
  } else {
    if (fat_volume_path(file_info, buf) < 0) {
      log_error("open file %s missing fat path\n", node->name);
      return -1;
    }

    if (file_info->fil.obj.fs == NULL) {
      BYTE fat_mode = FA_READ;
      if ((mode & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) != 0) {
        fat_mode = FA_READ | FA_WRITE;
      }
      int res = f_open(&file_info->fil, buf, fat_mode);
      if (res != FR_OK && fat_mode != FA_READ) {
        res = f_open(&file_info->fil, buf, FA_READ);
      }
      if (res != FR_OK) {
        log_error("open file %s path %s error code %d\n", node->name, buf, res);
        return -1;
      }
      node->length = (u32)f_size(&file_info->fil);
    }
  }
  return 1;
}

int find_in_dir(DIR *dp, FILINFO *fno, char *name) {
  FRESULT res = FR_NO_FILE;
  for (;;) {
    res = f_readdir(dp, fno); /* Get a directory item */
    if (res != FR_OK || !fno || !fno->fname[0]) {
      res = FR_NO_FILE;
      break;
    }
    // kprintf("%s=%s\n", fno->fname, name);
    if (kstrcmp(fno->fname, name) == 0) {
      res = FR_OK;
      break;
    }
  }
  return res;
}

vnode_t *fat_op_find(vnode_t *node, char *name) {
  file_info_t *file_info = node->data;
  DIR dir;
  FILINFO find_file;

  uint res = -1;
  char buf[MAX_FILE_PATH];
  if ((node->flags & V_BLOCKDEVICE) == V_BLOCKDEVICE) {
    if (file_info == NULL || fat_volume_path(file_info, buf) < 0) {
      log_error("fat find %s missing volume root\n", name);
      return NULL;
    }
    res = f_opendir(&dir, buf);
  } else {
    if (file_info == NULL || fat_volume_path(file_info, buf) < 0) {
      log_error("fat find %s in %s missing dir path\n", name, node->name);
      return NULL;
    }
    res = f_opendir(&dir, buf);
  }
  if (res != FR_OK) {
    log_error("bad dir %s code %d\n", name, res);
    return NULL;
  }
  uint type = V_FILE;

  file_info_t *new_file_info = kmalloc(sizeof(file_info_t), KERNEL_TYPE);
  kmemset(new_file_info, 0, sizeof(file_info_t));
  // find file in dir
  res = find_in_dir(&dir, &find_file, name);
  f_closedir(&dir);
  if (res != FR_OK) {
    log_error("not found file %s in %s code %d\n", name, node->name, res);
    kfree(new_file_info);
    return NULL;
  }

  if (file_info != NULL) {
    new_file_info->fs = file_info->fs;
  }
  fat_copy_vol(new_file_info, file_info != NULL ? file_info : NULL);
  kmemcpy(&new_file_info->file, &find_file, sizeof(FILINFO));

  const char *parent_fat_path = "/";
  if (file_info != NULL && file_info->fat_path[0] != '\0') {
    parent_fat_path = file_info->fat_path;
  }
  if (fat_join_path(parent_fat_path, name, new_file_info->fat_path,
                    sizeof(new_file_info->fat_path)) < 0) {
    log_error("fat find path too long %s/%s\n", parent_fat_path, name);
    kfree(new_file_info);
    return NULL;
  }

  if ((new_file_info->file.fattrib & AM_DIR) == AM_DIR) {
    type = V_DIRECTORY;
  } else if ((new_file_info->file.fattrib & AM_ARC) == AM_ARC) {
    type = V_FILE;
  }

  vnode_t *file = vfs_create_node(name, type);
  file->data = new_file_info;
  file->device = node->device;
  if (type == V_FILE) {
    file->length = new_file_info->file.fsize;
  }
  fat_init_op(file);

  return file;
}

uint fat_op_read_dir(vnode_t *node, struct vdirent *dirent, u32 *offset,
                     uint count) {
  if (!((node->flags & V_FILE) == V_FILE ||
        (node->flags & V_DIRECTORY) == V_DIRECTORY)) {
    log_debug("read dir failed for not file flags is %x\n", node->flags);
    return 0;
  }
  char buf[MAX_FILE_PATH];
  int res;
  file_info_t *file_info = node->data;
  if (file_info == NULL) {
    file_info = kmalloc(sizeof(file_info_t), KERNEL_TYPE);
    kmemset(file_info, 0, sizeof(file_info_t));
    node->data = file_info;
    vnode_t *super_node = fat_super_node(node);
    if (super_node != NULL && super_node->data != NULL) {
      fat_init_file_info_from_node(node, file_info, super_node->data);
    }
  }
  if (fat_volume_path(file_info, buf) < 0) {
    return 0;
  }

  u32 start = offset != NULL ? *offset : file_info->offset;
  FILINFO fno;

  /* 【性能：目录列举必须 O(N)，不能 O(N^2)】
   * 上层 getdents64 是【一次取一条】调进来的；原来这里每次都
   * close+opendir+从头逐条 f_readdir 跳到 offset ⇒ 列 N 个文件要
   * N^2/2 次目录扫描（每次还重新读盘，块缓存又是关的）⇒ roms 下几百个
   * 文件时"读目录像卡死"。
   * 现在：DIR 保持打开，连续读（start == 上次位置 dir_pos）直接接着
   * f_readdir；只有跳转（seek/首次）才重开并跳到 start。
   * DIR 的最终关闭交给 fat_op_close / fat_op_open。 */
  if (file_info->dir.obj.fs != NULL && file_info->dir_pos != (int)start) {
    f_closedir(&file_info->dir);
  }
  if (file_info->dir.obj.fs == NULL) {
    res = f_opendir(&file_info->dir, buf);
    if (res != FR_OK) {
      return 0;
    }
    file_info->dir_pos = 0;
  }
  while (file_info->dir_pos < (int)start) { /* 跳转：跳过前 start 条 */
    fno.fname[0] = 0;
    if (f_readdir(&file_info->dir, &fno) != FR_OK || fno.fname[0] == 0) {
      f_closedir(&file_info->dir);
      return 0;
    }
    file_info->dir_pos++;
  }

  uint i = (uint)file_info->dir_pos;
  uint nbytes = 0;
  uint read_count = 0;

  while (read_count < count) {
    fno.fname[0] = 0;
    res = f_readdir(&file_info->dir, &fno);
    if (res != FR_OK || fno.fname[0] == 0) {
      break;
    }

    if ((fno.fattrib & AM_DIR) == AM_DIR) {
      dirent->type = DT_DIR;
    } else if ((fno.fattrib & AM_ARC) == AM_ARC) {
      dirent->type = DT_REG;
    }

    kstrcpy(dirent->name, fno.fname);
    dirent->ino = i + 1;
    dirent->offset = i + 1;
    {
      u32 n = kstrlen(fno.fname) + 1;
      u32 reclen = 19 + n;
      dirent->length = (u16)((reclen + 7) & ~7u);
    }
    nbytes += dirent->length;
    dirent++;
    i++;
    file_info->dir_pos = (int)i;
    file_info->offset = (int)i;
    if (offset != NULL) {
      *offset = i;
    }
    read_count++;
  }

  return nbytes;
}

int fat_op_close(vnode_t *node) {
  file_info_t *file_info = node->data;
  if (file_info != NULL) {
    file_info->offset = 0;
    if ((node->flags & V_DIRECTORY) == V_DIRECTORY) {
      if (file_info->dir.obj.fs != NULL) {
        f_closedir(&file_info->dir);
      }
      file_info->dir_pos = 0;
    } else if (file_info->fil.obj.fs != NULL) {
      f_close(&file_info->fil);
      file_info->fil.obj.fs = NULL;
    }
  }
  return 0;
}

size_t fat_op_ioctl(struct vnode *node, uint cmd, void *args) {
  uint ret = 0;
  // log_debug("fat_op_ioctl %x\n", cmd);

  file_info_t *file_info = node->data;
  if (file_info == NULL) {
    log_error("fat_op_ioctl faild file_info is null\n");
    return -1;
  }

  if (cmd == IOC_STAT) {
    struct stat *stat = args;
    FILINFO fno;
    char buf[MAX_FILE_PATH];
    if (fat_volume_path(file_info, buf) < 0) {
      return -1;
    }
    int res = f_stat(buf, &fno);
    if (res != FR_OK) {
      log_error("get file info error %s code %d\n", node->name, res);
      ret = -1;
      return ret;
    }

    /* 整块清零再填：st_dev/st_ino/st_uid/st_gid/st_rdev/st_blocks 这些
     * 本文件系统不提供的字段必须是 0，不能留用户栈上的旧值（libc 侧会读）。 */
    kmemset(stat, 0, sizeof(*stat));
    if ((fno.fattrib & AM_DIR) == AM_DIR) {
      stat->st_size = fno.fsize;
      stat_set_mtime(stat, (time_t)(fno.fdate << 16 | fno.ftime));
      stat->st_mode = S_IFDIR;
      stat->st_mode |= 0755;  // rwxr-xr-x
    } else {
      stat->st_size = fno.fsize;
      stat_set_mtime(stat, (time_t)(fno.fdate << 16 | fno.ftime));
      stat->st_mode = S_IFREG;
      stat->st_mode |= 0644;  // rw-r--r--
    }

    return 0;
  } else if (cmd == IOC_STATFS) {
    struct statfs *stat = args;

    return 0;
  }

  device_t *dev = node->device;
  if (dev == NULL) {
    return ret;
  }
  // va_list args;
  // va_start(args, cmd);
  ret = dev->ioctl(dev, cmd, args);
  // va_end(args);
  return ret;
}

voperator_t fat_op = {
    .read = fat_op_read,
    .write = fat_op_write,
    .open = fat_op_open,
    .close = fat_op_close,
    .find = fat_op_find,
    .readdir = fat_op_read_dir,
    .ioctl = fat_op_ioctl,
};

void fat_init_op(vnode_t *node) { node->op = &fat_op; }

static int fat_publish(device_t *dev, const char *name, int index) {
  vnode_t *node;
  char *ncopy;
  if (dev == NULL || name == NULL || index < 0 || index >= 4) {
    return -1;
  }
  node = devfs_create_device(dev);
  if (node == NULL) {
    log_error("fatfs: devfs_create_device failed for %s\n", name);
    return -1;
  }
  ncopy = kmalloc(8, KERNEL_TYPE);
  if (ncopy == NULL) {
    return -1;
  }
  kmemset(ncopy, 0, 8);
  kstrncpy(ncopy, name, 7);
  node->name = ncopy;
  vfs_mount(NULL, "/dev", node);
  fat_vols[index].node = node;
  kmemset(fat_vols[index].name, 0, sizeof(fat_vols[index].name));
  kstrncpy(fat_vols[index].name, ncopy, 7);
  fat_vols[index].mounted = 0;
  fat_pdrv_dev[index] = dev;
  return 0;
}

static int fat_bind(int index) {
  file_info_t *fi;
  char path[4];
  FRESULT res;
  if (index < 0 || index >= fat_vol_count || fat_vols[index].node == NULL) {
    return -1;
  }
  if (fat_vols[index].mounted) {
    return 0;
  }
  fi = kmalloc(sizeof(file_info_t), KERNEL_TYPE);
  if (fi == NULL) {
    return -1;
  }
  kmemset(fi, 0, sizeof(file_info_t));
  path[0] = (char)('0' + index);
  path[1] = ':';
  path[2] = 0;
  fi->vol[0] = path[0];
  fi->vol[1] = ':';
  fi->vol[2] = 0;
  kstrcpy(fi->fat_path, "/");
  fat_pdrv_dev[index] = fat_vols[index].node->device;
  res = f_mount(&fi->fs, path, 1);
  if (res != FR_OK) {
    f_mount(0, path, 0);
    kfree(fi);
    if (res == FR_NO_FILESYSTEM) {
      log_info("fatfs: %s is not fat\n", fat_vols[index].name);
      return -2;
    }
    log_error("fatfs: mount %s failed %d\n", fat_vols[index].name, res);
    return -1;
  }
  fat_vols[index].node->data = fi;
  fat_init_op(fat_vols[index].node);
  fat_vols[index].mounted = 1;
  log_info("fatfs: %s volume %s\n", fat_vols[index].name, path);
  return 0;
}

static int fat_split_parent(const char *path, char *parent, char *leaf) {
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

static vnode_t *fat_ensure_dir(const char *path) {
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
  if (fat_split_parent(path, parent, leaf) != 0) {
    return NULL;
  }
  if (fat_ensure_dir(parent) == NULL) {
    return NULL;
  }
  dir = vfs_create_node((u8 *)leaf, V_DIRECTORY);
  vfs_mount(NULL, (u8 *)parent, dir);
  return vfs_find(NULL, (u8 *)path);
}

static int fat_mount_at(int index, const char *path) {
  vnode_t *exist;
  vnode_t *mp;
  char parent[96];
  char leaf[32];
  if (index <= 0 || index >= fat_vol_count) {
    return -1;
  }
  if (path == NULL || path[0] != '/' || path[1] == 0) {
    return -1;
  }
  if (kstrncmp(path, "/dev", 4) == 0 && (path[4] == 0 || path[4] == '/')) {
    return -1;
  }
  {
    int rc = fat_bind(index);
    if (rc != 0) {
      return rc;
    }
  }
  exist = vfs_find(NULL, (u8 *)path);
  if (exist != NULL) {
    if ((exist->flags & V_DIRECTORY) == 0) {
      log_warn("fatfs: %s is not a directory\n", path);
      return -1;
    }
    exist->super = fat_vols[index].node;
    log_info("fatfs: mount %s on %s\n", fat_vols[index].name, path);
    return 0;
  }
  if (fat_split_parent(path, parent, leaf) != 0) {
    return -1;
  }
  if (fat_ensure_dir(parent) == NULL) {
    return -1;
  }
  mp = vfs_create_node((u8 *)leaf, V_DIRECTORY);
  if (mp == NULL) {
    return -1;
  }
  mp->super = fat_vols[index].node;
  vfs_mount(NULL, (u8 *)parent, mp);
  log_info("fatfs: mount %s on %s\n", fat_vols[index].name, path);
  return 0;
}

static int fat_mount_cb(const char *key, const char *val, void *user) {
  int i;
  (void)user;
  if (key == NULL || val == NULL || val[0] == 0) {
    return 1;
  }
  /* sda/sdb 是整卡，cat 用来看分区表，不挂成目录。sda1 已经在 /。 */
  if (kstrcmp(key, "sda") == 0 || kstrcmp(key, "sdb") == 0) {
    return 1;
  }
  if (fat_vol_count > 0 && kstrcmp(key, fat_vols[0].name) == 0) {
    return 1;
  }
  for (i = 1; i < fat_vol_count; i++) {
    if (kstrcmp(fat_vols[i].name, key) == 0) {
      int rc = fat_mount_at(i, val);
      if (rc != 0 && rc != -2) {
        log_warn("fatfs: mount %s on %s failed\n", key, val);
      }
      return 1;
    }
  }
  log_warn("fatfs: unknown partition %s\n", key);
  return 1;
}

static void fat_mount_apply(void) {
  int i;
  int n;
  n = sysconf_foreach("mount", fat_mount_cb, NULL);
  if (n > 0) {
    return;
  }
  if (fat_ensure_dir("/mnt") == NULL) {
    log_warn("fatfs: cannot create /mnt\n");
    return;
  }
  for (i = 1; i < fat_vol_count; i++) {
    char path[16];
    kstrcpy(path, "/mnt/");
    kstrcat(path, fat_vols[i].name);
    {
      int rc = fat_mount_at(i, path);
      if (rc != 0 && rc != -2) {
        log_warn("fatfs: auto mount %s failed\n", fat_vols[i].name);
      }
    }
  }
}

void fat_init(void) {
  int i;
  int n = 0;
  vnode_t *root;
  log_info("fatfs init\n");
  for (i = 0; i < 4; i++) {
    device_t *dev = device_find(DEVICE_SATA0 + i);
    const char *name;
    if (dev == NULL) {
      break;
    }
    name = (dev->name != NULL) ? dev->name : "sda";
    if (fat_publish(dev, name, n) == 0) {
      n++;
    }
  }
  if (n == 0) {
    device_t *dev = device_find(DEVICE_SATA);
    if (dev != NULL && fat_publish(dev, "sda", 0) == 0) {
      n = 1;
    }
  }
  fat_vol_count = n;
  if (n == 0) {
    log_error("fatfs: no block device found\n");
    return;
  }
  root = vfs_find(NULL, "/");
  if (fat_bind(0) == 0 && root != NULL) {
    root->super = fat_vols[0].node;
    default_node = fat_vols[0].node;
    log_info("fatfs: mount %s on /\n", fat_vols[0].name);
  } else {
    log_error("fatfs: mount %s on / failed\n", fat_vols[0].name);
  }
  sysconf_on_ready(fat_mount_apply);
  log_info("fatfs init end\n");
}

void fat_exit(void) { log_info("fatfs exit\n"); }

module_t fatfs_module = {.name = "fatfs", .init = fat_init, .exit = fat_exit};