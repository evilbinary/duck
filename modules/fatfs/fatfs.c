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
  int offset;
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

int MMC_disk_initialize() {
  log_debug("MMC_disk_initialize\n");
  return RES_OK;
}

int MMC_disk_status() { return RES_OK; }

int MMC_disk_read(char *buffer, LBA_t sector, int count) {
  // log_debug("MMC_disk_read %x %d buffer %x\n", sector, count, buffer);

  u32 offset = sector * FF_MIN_SS;
  u32 length = count * FF_MIN_SS;
  u32 ret = RES_OK;
  fat_device_read(default_node, offset, length, buffer);
  // log_debug("MMC_disk_read end %x %d buffer %x\n", sector, count, buffer);
  return ret;
}

int MMC_disk_write(char *buffer, LBA_t sector, int count) {
  u32 ret = RES_OK;
  if (sector < 0 || count < 0) {
    return ret;
  }
  u32 offset = sector * FF_MIN_SS;
  u32 length = count * FF_MIN_SS;

  // log_debug("MMC_disk_write %x %d buffer %x\n", sector, count, buffer);

  fat_device_write(default_node, offset, length, buffer);

  return ret;
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
  if (time_fd < 0) return 0;

  int ret = sys_read(time_fd, &time, sizeof(rtc_time_t));
  if (ret < 0) {
    log_error("erro read time\n");
    return 0;
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

  if (offset >= 0) {
    f_lseek(&file_info->fil, offset);
  }
  // kprintf("read file-->%s fil: %x offset %d\n", node->name, &file_info->fil,
  //         offset);
  
  //FFOBJID* obj=&file_info->fil.obj;
  //kprintf("read =>%x %x type %x %x %x\n",obj, obj->fs , obj->fs->fs_type , obj->id , obj->fs->id);

  int readbytes = 0;
  int res = f_read(&file_info->fil, buffer, nbytes, &readbytes);
  if (res != FR_OK) {
    log_error("fat read %s error code %d\n", node->name, res);
    return -1;
  }

  // log_debug("fat_op_read offset %x nbytes %d readbytes %d ret=%d\n", offset,
            // nbytes, readbytes, res);
  // print_hex(buffer,readbytes);

  return readbytes;
}

u32 fat_op_write(vnode_t *node, u32 offset, size_t nbytes, u8 *buffer) {
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

u32 fat_op_open(vnode_t *node, u32 mode) {
  char *name = node->name;
  file_info_t *file_info = node->data;
  char buf[MAX_FILE_PATH];

  if (file_info == NULL) {
    file_info = kmalloc(sizeof(file_info_t), DEFAULT_TYPE);
    file_info_t *super_file_info = node->super->data;
    file_info->fs = super_file_info->fs;
    node->data = file_info;
  }

  if (file_info == NULL) {
    log_error("fat may init faild\n");
    return -1;
  }

  file_info->offset = 0;

  if ((mode & O_CREAT) == O_CREAT) {
    log_debug("create new file %s\n", name);
    kstrcpy(buf, VOLUME);
    vfs_path_append(node, NULL, &buf[2]);
    int res = f_open(&file_info->fil, buf, FA_READ | FA_WRITE | FA_OPEN_ALWAYS);
    if (res != FR_OK) {
      log_error("open create file %s error code %d\n", node->name, res);
      return -1;
    }
    // kprintf("create file_info->fil->%x buf %s\n", &file_info->fil, buf);

  } else if ((mode & O_DIRECTORY) == O_DIRECTORY ||
             (node->flags & V_DIRECTORY) == V_DIRECTORY) {
    kstrcpy(buf, VOLUME);
    vfs_path_append(node, "", &buf[2]);
    int res = f_opendir(&file_info->dir, buf);
    if (res != FR_OK) {
      log_error("open dir %s error code %d\n", node->name, res);
      return -1;
    }
  } else {
    kstrcpy(buf, VOLUME);
    vfs_path_append(node, NULL, &buf[2]);

    // kprintf("file_info->fil->%x path: %s\n", &file_info->fil,buf);

    if (file_info->fil.obj.fs == NULL) {
      int res = f_open(&file_info->fil, buf, FA_READ | FA_WRITE);
      if (res != FR_OK) {
        log_error("open file %s path %s error code %d\n", node->name, buf, res);
        return -1;
      }
      //dont not foget length
      node->length = file_info->file.fsize;
    }else{
      //kprintf("2file_info->fil->%x path: %s\n", &file_info->fil,buf);
      log_error("get fil %x  path: %s\n",&file_info->fil,buf);

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

  u32 res = -1;
  char buf[MAX_FILE_PATH];
  if ((node->flags & V_BLOCKDEVICE) == V_BLOCKDEVICE) {
    res = f_opendir(&dir, VOLUME_ROOT);
  } else {
    kstrcpy(buf, VOLUME);
    vfs_path_append(node, NULL, &buf[2]);
    res = f_opendir(&file_info->dir, buf);
    kmemcpy(&dir, &file_info->dir, sizeof(DIR));
    res = FR_OK;
  }
  if (res != FR_OK) {
    log_error("bad dir %s code %d\n", name, res);
    return NULL;
  }
  u32 type = V_FILE;

  file_info_t *new_file_info = kmalloc(sizeof(file_info_t), DEFAULT_TYPE);
  // find file in dir
  res = find_in_dir(&dir, &find_file, name);
  if (res != FR_OK) {
    log_error("not found file %s in %s code %d\n", name, node->name, res);
    return NULL;
  }

  if (file_info != NULL) {
    new_file_info->fs = file_info->fs;
  }
  kmemcpy(&new_file_info->file, &find_file, sizeof(FILINFO));
  kmemcpy(&new_file_info->dir, &file_info->dir, sizeof(DIR));

  if ((new_file_info->file.fattrib & AM_DIR) == AM_DIR) {
    type = V_DIRECTORY;
    if ((node->flags & V_BLOCKDEVICE) == V_BLOCKDEVICE) {
      kstrcpy(buf, VOLUME_ROOT);
      kstrcpy(&buf[3], name);
    }

  } else if ((new_file_info->file.fattrib & AM_ARC) == AM_ARC) {
    kstrcpy(buf, VOLUME);
    int ret = vfs_path_append(node, name, &buf[3]);
    int res = f_open(&new_file_info->fil, buf, FA_READ | FA_WRITE);

    // kprintf("file--->%s fil: %x\n", name, &new_file_info->fil);
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
  int res;
  file_info_t *file_info = node->data;
  if (file_info == NULL) {
    file_info = kmalloc(sizeof(file_info_t), DEFAULT_TYPE);
    node->data = file_info;
    kstrcpy(buf, VOLUME);
    int ret = vfs_path_append(node, NULL, &buf[2]);
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
    kstrcpy(buf, VOLUME);
    int ret = vfs_path_append(node, "", &buf[2]);
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
    name = kmalloc(4, DEFAULT_TYPE);
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

  file_info_t *file_info = kmalloc(sizeof(file_info_t), DEFAULT_TYPE);

  int res = f_mount(&file_info->fs, VOLUME, 0);
  if (res != FR_OK) {
    log_error("mount fs error code %d\n", res);
  }

  node->data = file_info;
  log_info("fatfs init end\n");
}

void fat_exit(void) { log_info("fatfs exit\n"); }

module_t fatfs_module = {.name = "fatfs", .init = fat_init, .exit = fat_exit};