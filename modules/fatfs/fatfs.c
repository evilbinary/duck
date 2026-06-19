#include "diskio.h"
#include "ff.h"
#include "kernel/device.h"
#include "kernel/memory.h"
#include "kernel/stat.h"
#include "posix/sysfn.h"
#include "rtc/rtc.h"

// Select the block-device IOCTL encoding that matches the active block driver.
// ARM32/ARM64 use SDHCI (magic 's'); non-ARM platforms typically use AHCI (magic 'a').
#if defined(ARM) || defined(ARM64) || defined(__aarch64__)
#include "mmc/sdhci.h"
#else
#include "ahci/ahci.h"
#endif

#define VOLUME _T("1:")

#define VOLUME_ROOT _T("1:/")

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
  char fat_path[MAX_FILE_PATH];
} file_info_t;

vnode_t *default_node = NULL;

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

static int fat_volume_path(const file_info_t *file_info, char *buf) {
  if (file_info == NULL || file_info->fat_path[0] == '\0') {
    return -1;
  }
  kstrcpy(buf, VOLUME);
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
  if (file_info->fat_path[0] != '\0') {
    return;
  }
  if (node->name != NULL && kstrcmp(node->name, "/") == 0 &&
      super_file_info->fat_path[0] != '\0') {
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
  if (dev == NULL) {
    return ret;
  }
  dev->ioctl(dev, IOC_WRITE_OFFSET, offset);
  ret = dev->read(dev, buffer, nbytes);
  return ret;
}

static uint fat_device_write(vnode_t *node, uint offset, size_t nbytes,
                            u8 *buffer) {
  uint ret = 0;
  device_t *dev = (device_t *)node->device;
  if (dev == NULL) {
    return ret;
  }
  dev->ioctl(dev, IOC_WRITE_OFFSET, offset);
  ret = dev->write(dev, buffer, nbytes);
  return ret;
}

int MMC_disk_initialize() {
  log_debug("MMC_disk_initialize\n");
  return RES_OK;
}

int MMC_disk_status() { return RES_OK; }

int MMC_disk_read(char *buffer, LBA_t sector, int count) {
  //log_debug("MMC_disk_read sector=%d count=%d buffer=%x\n", sector, count, buffer);

  uint offset = sector * FF_MIN_SS;
  uint length = count * FF_MIN_SS;
  
  if (default_node == NULL) {
    log_error("MMC_disk_read: default_node is NULL\n");
    return RES_ERROR;
  }
  
  //log_debug("MMC_disk_read: default_node=%x device=%x\n", default_node, default_node->device);
  
  uint ret = fat_device_read(default_node, offset, length, buffer);
  if (ret != length) {
    log_error("MMC_disk_read: read failed at sector %d, expected %d bytes, got %d\n", 
              sector, length, ret);
    return RES_ERROR;
  }
  //log_debug("MMC_disk_read end sector=%d\n", sector);
  return RES_OK;
}

int MMC_disk_write(char *buffer, LBA_t sector, int count) {
  if (sector < 0 || count < 0) {
    return RES_PARERR;
  }
  
  if (default_node == NULL) {
    log_error("MMC_disk_write: default_node is NULL\n");
    return RES_ERROR;
  }
  
  uint offset = sector * FF_MIN_SS;
  uint length = count * FF_MIN_SS;

  // log_debug("MMC_disk_write %x %d buffer %x\n", sector, count, buffer);

  uint ret = fat_device_write(default_node, offset, length, buffer);
  if (ret != length) {
    log_error("MMC_disk_write: write failed at sector %d, expected %d bytes, got %d\n", 
              sector, length, ret);
    return RES_ERROR;
  }

  return RES_OK;
}

int MMC_disk_ioctl(u8 pdrv, u8 cmd, void *buff) {
  // log_debug("MMC_disk_ioctl cmd %d buff %x\n",cmd,buff);
  DWORD *pdword = NULL;
  WORD *pword = NULL;
  switch (cmd) {
    case GET_SECTOR_COUNT:
      pdword = (DWORD *)buff;
      *pdword = 9999999 + 1;
      return RES_OK;

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
  }
  return RES_OK;
}

uint get_fattime(void) {
  rtc_time_t time;
  time.day = 1;
  time.hour = 0;
  time.minute = 0;
  time.month = 1;
  time.second = 0;
  time.year = 1900;

  int time_fd = -1;
  time_fd = sys_open("/dev/time", 0);
  if (time_fd < 0) return 0;

  int ret = sys_read(time_fd, &time, sizeof(rtc_time_t));
  if (ret < 0) {
    log_error("erro read time\n");
    return 0;
  }

  return (uint)(time.year - 80) << 25 | (uint)(time.month + 1) << 21 |
         (uint)time.day << 16 | (uint)time.hour << 11 | (uint)time.minute << 5 |
         (uint)time.second >> 1;
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

  if (offset >= 0) {
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
  if (offset >= 0) {
    f_lseek(&file_info->fil, offset);
  }

  int readbytes = 0;
  int res = f_write(&file_info->fil, buffer, nbytes, &readbytes);
  if (res != FR_OK) {
    log_error("fat write %s error code %d\n", node->name, res);
    return -1;
  }

  return readbytes;
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
    if (fat_volume_path(file_info, buf) < 0) {
      return -1;
    }
    int res = f_open(&file_info->fil, buf, FA_READ | FA_WRITE | FA_OPEN_ALWAYS);
    if (res != FR_OK) {
      log_error("open create file %s error code %d\n", node->name, res);
      return -1;
    }
    // kprintf("create file_info->fil->%x buf %s\n", &file_info->fil, buf);

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
  } else {
    if (fat_volume_path(file_info, buf) < 0) {
      log_error("open file %s missing fat path\n", node->name);
      return -1;
    }

    if (file_info->fil.obj.fs == NULL) {
      int res = f_open(&file_info->fil, buf, FA_READ | FA_WRITE);
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
    res = f_opendir(&dir, VOLUME_ROOT);
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
  res = f_opendir(&file_info->dir, buf);
  if (res != FR_OK) {
    return 0;
  }

  uint i = 0;
  uint nbytes = 0;
  uint read_count = 0;
  u32 start = offset != NULL ? *offset : file_info->offset;
  FILINFO fno;

  while (true) {
    fno.fname[0] = 0;
    res = f_readdir(&file_info->dir, &fno);
    if (res != FR_OK || fno.fname[0] == 0) {
      break;
    }

    if (i < start) {  // 定位到某个文件数量开始
      i++;
      continue;
    }
    if (read_count < count) {
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
      dirent++;  // maybe change to offset
      file_info->offset = i + 1;
      if (offset != NULL) {
        *offset = i + 1;
      }
      read_count++;
    } else {
      break;
    }
    i++;
  }
  f_closedir(&file_info->dir);

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

    if ((fno.fattrib & AM_DIR) == AM_DIR) {
      stat->st_size = fno.fsize;
      stat->st_mtim = fno.fdate << 16 | fno.ftime;
      stat->st_mode = S_IFDIR;
      stat->st_mode |= 0755;  // rwxr-xr-x
    } else {
      stat->st_size = fno.fsize;
      stat->st_mtim = fno.fdate << 16 | fno.ftime;
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

void fat_init(void) {
  log_info("fatfs init\n");

  char *name;
  vnode_t *root_super = NULL;
  for (int i = 0; i < 3; i++) {
    device_t *dev = device_find(DEVICE_SATA + i);
    if (dev == NULL) {
      continue;
    }
    name = kmalloc(4, KERNEL_TYPE);
    name[0] = 's';
    name[1] = 'd';
    name[2] = 0x61 + i;
    name[3] = 0;
    vnode_t *node_sda = devfs_create_device(dev);
    if (node_sda == NULL) {
      log_error("fatfs: devfs_create_device failed for dev %d\n", dev->id);
      continue;
    }
    node_sda->name = name;
    vfs_mount(NULL, "/dev", node_sda);
    if (root_super == NULL) {
      root_super = node_sda;
      break;
    }
  }

  if (root_super == NULL) {
    log_error("fatfs: no block device found (DEVICE_SATA..)\n");
    return;
  }

  // auto mount first dev as root
  vnode_t *root = vfs_find(NULL, "/");
  root->super = root_super;

  vnode_t *node = vfs_find(NULL, "/dev/sda");
  default_node = node;
  if (node == NULL) {
    log_error("not found sda\n");
    return;
  }
  fat_init_op(node);

  file_info_t *file_info = kmalloc(sizeof(file_info_t), KERNEL_TYPE);
  kmemset(file_info, 0, sizeof(file_info_t));

  int res = f_mount(&file_info->fs, VOLUME, 0);
  if (res != FR_OK) {
    log_error("mount fs error code %d\n", res);
  } else {
    log_info("mount fs success\n");
  }
  
#ifdef TEST
  // Test reading root directory
  DIR test_dir;
  res = f_opendir(&test_dir, VOLUME_ROOT);
  if (res != FR_OK) {
    log_error("f_opendir root failed code %d\n", res);
  } else {
    log_info("f_opendir root success\n");
    FILINFO fno;
    int count = 0;
    while (count < 10) {
      res = f_readdir(&test_dir, &fno);
      if (res != FR_OK || fno.fname[0] == 0) break;
      log_info("  file: %s\n", fno.fname);
      count++;
    }
    f_closedir(&test_dir);
  }
#endif

  kstrcpy(file_info->fat_path, "/");
  node->data = file_info;
  log_info("fatfs init end\n");
}

void fat_exit(void) { log_info("fatfs exit\n"); }

module_t fatfs_module = {.name = "fatfs", .init = fat_init, .exit = fat_exit};