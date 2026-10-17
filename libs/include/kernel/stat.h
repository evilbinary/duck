#ifndef	_SYS_STAT_H
#define	_SYS_STAT_H
#ifdef __cplusplus
extern "C" {
#endif


#include "time.h"
#include "type.h"


#define IOC_MAGIC 'f'
#define IOC_INIT _IO(IOC_MAGIC, 0)
#define IOC_READ_REG _IOW(IOC_MAGIC, 1, int)
#define IOC_WRITE_REG _IOW(IOC_MAGIC, 2, int)
#define IOC_STAT _IOW(IOC_MAGIC, 3, int)
#define IOC_STATFS _IOW(IOC_MAGIC, 4, int)


/* 【应用可见 ABI —— 必须与 libc 的 struct stat 逐字节一致】
 * 这个结构体的写入方是内核（fatfs/ext 的 IOC_STAT 直接把用户指针当目标），
 * 读取方是用户态 libc，两边布局必须完全一致。
 * 64 位架构（aarch64，musl）的布局是 128 字节：
 *   dev_t/ino_t/blkcnt_t = 64 位；mode/nlink/uid/gid/blksize = 32 位；
 *   三个时间戳是 16 字节的 struct timespec。
 * 而 type.h 里的 dev_t/ino_t/blkcnt_t 是 32 位（内核内部拿它们当设备号/块数
 * 用，保留不动），直接用它拼 struct stat 会让所有字段整体前移 8 字节：
 * st_size 落在 40 而 musl 从 48 读 ⇒ 应用拿到的"文件大小"实际是
 * st_blksize/__pad2 两个从未被写过的字段的残留（实测 772MB 的 llf
 * 被读成几百字节，yllm 直接 size mismatch）。这里按 ABI 宽度显式声明。
 * 32 位架构（armv7/mips 等）沿用旧布局。 */
#if defined(__aarch64__) || defined(__x86_64__) || defined(__LP64__) || defined(_LP64)
struct stat {
	unsigned long long st_dev;   /* 0  */
	unsigned long long st_ino;   /* 8  */
	unsigned int st_mode;        /* 16 */
	unsigned int st_nlink;       /* 20 */
	unsigned int st_uid;         /* 24 */
	unsigned int st_gid;         /* 28 */
	unsigned long long st_rdev;  /* 32 */
	unsigned long __pad;         /* 40 */
	off_t st_size;               /* 48 */
	int st_blksize;              /* 56 */
	int __pad2;                  /* 60 */
	long long st_blocks;         /* 64 */
	struct timespec st_atim;     /* 72 */
	struct timespec st_mtim;     /* 88 */
	struct timespec st_ctim;     /* 104 */
	unsigned __unused2[2];       /* 120, 总计 128 字节 */
};
#else
struct stat {
	dev_t st_dev;
	ino_t st_ino;
	mode_t st_mode;
	nlink_t st_nlink;
	uid_t st_uid;
	gid_t st_gid;
	dev_t st_rdev;
	unsigned long __pad;
	off_t st_size;
	blksize_t st_blksize;
	int __pad2;
	blkcnt_t st_blocks;
#if defined(__rtems__)
  struct timespec st_atim;
  struct timespec st_mtim;
  struct timespec st_ctim;
#else
	time_t st_atim;
	time_t st_mtim;
	time_t st_ctim;
#endif
	unsigned __unused2[2];
};
#endif

/* 填 mtime 的统一入口：64 位 ABI 下 st_mtim 是 struct timespec（16 字节，
 * 必须同时给出 tv_nsec），旧布局下是 time_t 标量。fatfs/fat 的 IOC_STAT
 * 都走这里，免得到处写 #if。 */
static inline void stat_set_mtime(struct stat* st, time_t sec) {
  if (st == NULL) {
    return;
  }
#if defined(__aarch64__) || defined(__x86_64__) || defined(__LP64__) || defined(_LP64)
  st->st_mtim.tv_sec = sec;
  st->st_mtim.tv_nsec = 0;
#else
  st->st_mtim = sec;
#endif
}


#define __NEED_dev_t
#define __NEED_ino_t
#define __NEED_mode_t
#define __NEED_nlink_t
#define __NEED_uid_t
#define __NEED_gid_t
#define __NEED_off_t
#define __NEED_time_t
#define __NEED_blksize_t
#define __NEED_blkcnt_t
#define __NEED_struct_timespec

#if defined(__rtems__)
#define st_atime st_atim.tv_sec
#define st_ctime st_ctim.tv_sec
#define st_mtime st_mtim.tv_sec
#endif

// #define st_atime st_atim.tv_sec
// #define st_mtime st_mtim.tv_sec
// #define st_ctime st_ctim.tv_sec

#define S_IFMT  0170000

#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFBLK 0060000
#define S_IFREG 0100000
#define S_IFIFO 0010000
#define S_IFLNK 0120000
#define S_IFSOCK 0140000

#define S_TYPEISMQ(buf)  0
#define S_TYPEISSEM(buf) 0
#define S_TYPEISSHM(buf) 0
#define S_TYPEISTMO(buf) 0

#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#define S_ISCHR(mode)  (((mode) & S_IFMT) == S_IFCHR)
#define S_ISBLK(mode)  (((mode) & S_IFMT) == S_IFBLK)
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#define S_ISFIFO(mode) (((mode) & S_IFMT) == S_IFIFO)
#define S_ISLNK(mode)  (((mode) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(mode) (((mode) & S_IFMT) == S_IFSOCK)

#ifndef S_IRUSR
#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXU 0700
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IRWXG 0070
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#define S_IRWXO 0007
#endif

#define UTIME_NOW  0x3fffffff
#define UTIME_OMIT 0x3ffffffe

struct statx {
	uint32_t stx_mask;
	uint32_t stx_blksize;
	uint64_t stx_attributes;
	uint32_t stx_nlink;
	uint32_t stx_uid;
	uint32_t stx_gid;
	uint16_t stx_mode;
	uint16_t pad1;
	uint64_t stx_ino;
	uint64_t stx_size;
	uint64_t stx_blocks;
	uint64_t stx_attributes_mask;
	struct {
		int64_t tv_sec;
		uint32_t tv_nsec;
		int32_t pad;
	} stx_atime, stx_btime, stx_ctime, stx_mtime;
	uint32_t stx_rdev_major;
	uint32_t stx_rdev_minor;
	uint32_t stx_dev_major;
	uint32_t stx_dev_minor;
	uint64_t spare[14];
};


#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1


struct statfs {
   long    f_type;     /* type of filesystem */
   long    f_bsize;    /* optimal transfer block size */
   long    f_blocks;   /* total data blocks in file system */
   long    f_bfree;    /* free blocks in fs */
   long    f_bavail;   /* free blocks avail to non-superuser */
   long    f_files;    /* total file nodes in file system */
   long    f_ffree;    /* free file nodes in fs */
   long    f_fsid;     /* file system id */
   long    f_namelen;  /* maximum length of filenames */
   long    f_spare[6]; /* spare for later */
};

#ifdef __cplusplus
}
#endif
#endif


