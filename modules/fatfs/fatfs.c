#include "diskio.h"
#include "ff.h"
#include "kernel/device.h"
#include "kernel/memory.h"
#include "kernel/stat.h"
#include "posix/sysfn.h"
#include "rtc/rtc.h"

#ifdef ARM
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
  u32 offset;
} file_info_t;

vnode_t *default_node = NULL;

static u32 fat_device_read(vnode_t *node, u32 offset, size_t nbytes,
                           u8 *buffer) {
  u32 ret = 0;
  device_t *dev = (device_t *)node->device;
  if (dev == NULL) {
    return ret;
  }
  dev->ioctl(dev, IOC_WRITE_OFFSET, offset);
  ret = dev->read(dev, buffer, nbytes);
  return ret;
}

static u32 fat_device_write(vnode_t *node, u32 offset, size_t nbytes,
                            u8 *buffer) {
  u32 ret = 0;
  device_t *dev = (device_t *)node->device;
  if (dev == NULL) {
    return ret;
  }
  dev->ioctl(dev, IOC_WRITE_OFFSET, offset);
  ret = dev->write(dev, buffer, nbytes);
  return ret;
}

void MMC_disk_initialize() { log_debug("MMC_disk_initialize\n"); }

int MMC_disk_status() { return RES_OK; }

int MMC_disk_read(char *buffer, LBA_t sector, int count) {
  // log_debug("MMC_disk_read %x %d\n",sector,count);

  u32 offset = sector * FF_MIN_SS;
  u32 length = count * FF_MIN_SS;
  u32 ret = RES_OK;
  fat_device_read(default_node, offset, length, buffer);

  return ret;
}

int MMC_disk_write(char *buffer, LBA_t sector, int count) {
  // log_debug("MMC_disk_write\n");

  u32 offset = sector * FF_MIN_SS;
  u32 length = count * FF_MIN_SS;
  u32 ret = RES_OK;

  fat_device_write(default_node, offset, length, buffer);

  return ret;
}

int MMC_disk_ioctl(u8 pdrv, u8 cmd, void *buff) {
  // log_debug("MMC_disk_ioctl\n");
  return RES_OK;
}

u32 get_fattime(void) {
  rtc_time_t time;
  time.day = 1;
  time.hour = 0;
  time.minute = 0;
  time.month = 1;
  time.second = 0;
  time.year = 1900;

  int time_fd = -1;
  time_fd = sys_open("/dev/time", 0);
  if (time_fd < 0) return;

  int ret = sys_read(time_fd, &time, sizeof(rtc_time_t));
  if (ret < 0) {
    log_error("erro read time\n");
    return;
  }

  return (u32)(time.year - 80) << 25 | (u32)(time.month + 1) << 21 |
         (u32)time.day << 16 | (u32)time.hour << 11 | (u32)time.minute << 5 |
         (u32)time.second >> 1;
}

static void print_hex(u8 *addr, u32 size) {
  for (int x = 0; x < size; x++) {
    kprintf("%02x ", addr[x]);
    if (x != 0 && (x % 32) == 0) {
      kprintf("\n");
    }
  }
  kprintf("\n\r");
}

u32 fat_op_read(vnode_t *node, u32 offset, size_t nbytes, u8 *buffer) {
  file_info_t *file_info = node->data;

  if (offset > 0) {
    f_lseek(&file_info->fil, offset);
  }
  // log_debug("fat_op_read %x %d\n", offset, nbytes);

  int readbytes = 0;
  int res = f_read(&file_info->fil, buffer, nbytes, &readbytes);
  if (res != FR_OK) {
    log_error("fat read %s error code %d\n", node->name, res);
    return -1;
  }
  // print_hex(buffer,nbytes);

  return nbytes;
}

u32 fat_op_write(vnode_t *node, u32 offset, size_t nbytes, u8 *buffer) {
  file_info_t *file_info = node->data;
  // log_debug("fat_op_write\n");
  f_lseek(&file_info->fil, offset);

  int readbytes = 0;
  int res = f_write(&file_info->fil, buffer, nbytes, &readbytes);
  if (res != FR_OK) {
    log_error("fat write %s error code %d\n", node->name, res);
    return -1;
  }

  return nbytes;
}

u32 fat_op_open(vnode_t *node, u32 mode) {
  file_info_t *file_info = node->data;
  char buf[MAX_FILE_PATH];
  if (file_info == NULL) {
    return 1;
  }
  if ((mode & O_CREAT) == O_CREAT) {
  } else if ((mode & O_DIRECTORY) == O_DIRECTORY) {
    char buf[MAX_FILE_PATH];
    kstrcpy(buf, VOLUME);
    vfs_path_append(node, "", &buf[2]);
    int res = f_opendir(&file_info->dir, buf);
    if (res != FR_OK) {
      log_error("open dir %s error code %d\n", node->name, res);
      return -1;
    }
  } else {
    kstrcpy(buf, VOLUME);
    vfs_path_append(node, "", &buf[2]);
    int res = f_open(&file_info->fil, buf, FA_READ | FA_WRITE);
    if (res != FR_OK) {
      log_error("open file %s error code %d\n", node->name, res);
      return -1;
    }
  }
  return 1;
}

vnode_t *fat_op_find(vnode_t *node, char *name) {
  file_info_t *file_info = node->data;
  u32 res = -1;
  char buf[MAX_FILE_PATH];
  if ((node->flags & V_BLOCKDEVICE) == V_BLOCKDEVICE) {
    res = f_opendir(&file_info->dir, VOLUME_ROOT);
  } else {
    res = FR_OK;
  }
  if (res != FR_OK) {
    log_error("bad dir %s code %d\n", name, res);
    return NULL;
  }

  u32 type = V_FILE;

  file_info_t *new_file_info = kmalloc(sizeof(file_info_t), KERNEL_TYPE);

  kmemcpy(&new_file_info->dir, &file_info->dir, sizeof(DIR));

  // find file in dir
  new_file_info->dir.pat = name;
  FILINFO find_file;
  res = f_findnext_match(&new_file_info->dir, &find_file);
  if (res != FR_OK) {
    log_error("bad fd %s in %s code %d\n", name, node->name, res);
    return NULL;
  }
  kmemcpy(&new_file_info->file, &find_file, sizeof(FILINFO));

  if (new_file_info->file.fattrib == AM_DIR) {
    type = V_DIRECTORY;
    if ((node->flags & V_BLOCKDEVICE) == V_BLOCKDEVICE) {
      kstrcpy(buf, VOLUME_ROOT);
      kstrcpy(&buf[3], name);
    } else {
      kstrcpy(buf, VOLUME);
      int ret = vfs_path_append(node, name, &buf[3]);
    }
    res = f_opendir(&new_file_info->dir, buf);
  }
  vnode_t *file = vfs_create_node(name, type);
  file->data = new_file_info;
  file->device = node->device;
  fat_init_op(file);

  return file;
}

u32 fat_op_read_dir(vnode_t *node, struct vdirent *dirent, u32 count) {
  if (!((node->flags & V_FILE) == V_FILE ||
        (node->flags & V_DIRECTORY) == V_DIRECTORY)) {
    log_debug("read dir failed for not file flags is %x\n", node->flags);
    return 0;
  }
  char buf[MAX_FILE_PATH];
  int res = 0;

  file_info_t *file_info = node->data;
  if (file_info == NULL) {
    file_info = kmalloc(sizeof(file_info_t), KERNEL_TYPE);
    node->data = file_info;

    kstrcpy(buf, VOLUME);
    int ret = vfs_path_append(node, "", &buf[2]);
    res = f_opendir(&file_info->dir, buf);
  }

  u32 i = 0;
  u32 nbytes = 0;
  u32 read_count = 0;
  FILINFO fno;

  while (true) {
    fno.fname[0] = 0;
    res = f_readdir(&file_info->dir, &fno);
    if (res != FR_OK || fno.fname[0] == 0) {
      break;
    }

    if (i < file_info->offset) {  // 定位到某个文件数量开始
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
      dirent->offset = i;
      dirent->length = sizeof(struct vdirent);
      nbytes += dirent->length;
      dirent++;  // maybe change to offset
      file_info->offset++;
      read_count++;
    } else {
      break;
    }
    i++;
  }
  file_info->offset = 0;
  f_closedir(&file_info->dir);

  return nbytes;
}

int fat_op_close(vnode_t *node) {
  file_info_t *file_info = node->data;
  if (file_info != NULL) {
    file_info->offset = 0;
    if (file_info->file.fattrib == AM_DIR) {
      f_closedir(&file_info->dir);
    } else {
      f_close(&file_info->fil);
    }
  }
  return 0;
}

size_t fat_op_ioctl(struct vnode *node, u32 cmd, void *args) {
  u32 ret = 0;
  log_debug("fat_op_ioctl\n");

  if (cmd == IOC_STAT) {
    file_info_t *file_info = node->data;
    if (file_info == NULL) {
      file_info = node->super->data;
      node->data = file_info;
    }
    struct stat *stat = args;
    FILINFO fno;
    int res = get_fileinfo(&file_info->dir, &fno);
    if (res != FR_OK) {
      log_error("get file info error %s code %d\n", node->name, res);
      ret = -1;
      return ret;
    }

    if ((fno.fattrib & AM_DIR) == AM_DIR) {
      stat->st_size = fno.fsize;
      stat->st_mtim = fno.fdate << 16 | fno.ftime;
      stat->st_mode = S_IFREG;
      stat->st_mode |= S_IFDIR;
    } else {
      stat->st_size = fno.fsize;
      stat->st_mtim = fno.fdate << 16 | fno.ftime;
      stat->st_mode = S_IFREG;
    }

    return 0;
  } else if (cmd == IOC_STATFS) {
    file_info_t *file_info = node->data;
    if (file_info == NULL) {
      file_info = node->super->data;
      node->data = file_info;
    }
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

void get_datetime(uint16_t *year, uint8_t *month, uint8_t *day, uint8_t *hour,
                  uint8_t *min, uint8_t *sec) {
  int time_fd = -1;
  time_fd = sys_open("/dev/time", 0);
  if (time_fd < 0) return;

  rtc_time_t time;
  time.day = 1;
  time.hour = 0;
  time.minute = 0;
  time.month = 1;
  time.second = 0;
  time.year = 1900;
  int ret = sys_read(time_fd, &time, sizeof(rtc_time_t));
  if (ret < 0) {
    log_error("erro read time\n");
    return;
  }
  *year = time.year;
  *month = time.month;
  *day = time.day;
  *hour = time.hour;
  *min = time.minute;
  *sec = time.second;
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
    node_sda->name = name;
    vfs_mount(NULL, "/dev", node_sda);
    if (root_super == NULL) {
      root_super = node_sda;
      break;
    }
  }

  // auto mount first dev as root
  vnode_t *root = vfs_find(NULL, "/");
  root->super = root_super;

  vnode_t *node = vfs_find(NULL, "/dev/sda");
  default_node = node;
  if (node == NULL) {
    log_error("not found sda\n");
  }
  fat_init_op(node);

  file_info_t *file_info = kmalloc(sizeof(file_info_t), KERNEL_TYPE);

  int res = f_mount(&file_info->fs, VOLUME, 0);
  if (res != FR_OK) {
    log_error("mount fs error code %d\n", res);
  }

  node->data = file_info;
  log_info("fatfs init end\n");
}

void fat_exit(void) { log_info("fatfs exit\n"); }

module_t fatfs_module = {.name = "fatfs", .init = fat_init, .exit = fat_exit};