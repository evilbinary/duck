/* 最小 procfs 垫片：动态生成 /proc 下进程/系统文件。
 * sys_open_kpath 在 fatfs 查找前调 procfs_lookup()；节点为 kmalloc 临时
 * vnode（name=路径，inode=内容种类，impl=pid/tid），read 现场生成，close 自释放。 */
#include "memory.h"
#include "thread.h"
#include "vfs.h"
#include "libs/include/kernel/string.h"

#define log_debug

extern thread_t* schedulable_head_thread[MAX_CPU];

#define PROC_STAT_PROC 1
#define PROC_STAT_TID 2
#define PROC_UPTIME 3
#define PROC_PID_MAX 4

static u32 proc_file_read(vnode_t* node, u32 offset, u32 size, u8* buf);
static u32 proc_task_readdir(vnode_t* node, vdirent_t* dirent, u32* offset,
                             u32 count);
static u32 proc_node_close(vnode_t* node);

static voperator_t proc_file_ops = {
    .read = proc_file_read,
    .close = proc_node_close,
};
static voperator_t proc_dir_ops = {
    .readdir = proc_task_readdir,
    .close = proc_node_close,
};

static void put_str(char** p, const char* end, const char* s) {
  while (*s && *p < end) {
    *(*p)++ = *s++;
  }
}

static void put_dec(char** p, const char* end, u32 v) {
  char t[12];
  int i = 0;
  if (v == 0) {
    t[i++] = '0';
  }
  while (v > 0 && i < 11) {
    t[i++] = (char)('0' + v % 10);
    v /= 10;
  }
  while (i > 0 && *p < end) {
    *(*p)++ = t[--i];
  }
}

static void put_char(char** p, const char* end, char c) {
  if (*p < end) {
    *(*p)++ = c;
  }
}

static int parse_dec(const char* s, u32* out) {
  u32 v = 0;
  int n = 0;
  while (s[n] >= '0' && s[n] <= '9') {
    v = v * 10 + (u32)(s[n] - '0');
    n++;
  }
  if (n > 0) {
    *out = v;
  }
  return n;
}

/* WAITING/SLEEP='S'（wait_for_threads 只认 'S'）、STOPPED='Z'、其余 'R' */
static char proc_state_char(thread_t* t) {
  if (t == NULL) {
    return 'Z';
  }
  if (t->state == THREAD_STOPPED) {
    return 'Z';
  }
  if (t->state == THREAD_RUNNING || t->state == THREAD_RUNABLE) {
    return 'R';
  }
  return 'S';
}

/* ===================== 内容生成 ===================== */

static int proc_generate(vnode_t* node, char* out, int outsz) {
  char* p = out;
  char* end = out + outsz - 1;

  if (node->inode == PROC_UPTIME) {
    extern u64 schedule_get_ticks();
    u64 ticks = schedule_get_ticks();
    u64 sec = ticks / SCHEDULE_FREQUENCY;
    u64 cent = (ticks % SCHEDULE_FREQUENCY) * 100 / SCHEDULE_FREQUENCY;
    put_dec(&p, end, (u32)sec);
    put_str(&p, end, ".");
    if (cent < 10) {
      put_char(&p, end, '0');
    }
    put_dec(&p, end, (u32)cent);
    put_char(&p, end, ' ');
    put_dec(&p, end, (u32)sec);
    put_str(&p, end, ".00\n");
  } else if (node->inode == PROC_PID_MAX) {
    put_str(&p, end, "4194304\n");
  } else if (node->inode == PROC_STAT_PROC || node->inode == PROC_STAT_TID) {
    thread_t* t = thread_find_id((int)node->impl);
    if (t == NULL) {
      return -1;
    }
    /* pid (comm) state ppid ... 后续字段补 0（madvise06 读第 12 字段=0） */
    put_dec(&p, end, node->impl);
    put_str(&p, end, " (");
    put_str(&p, end, t->name != NULL ? t->name : "unknown");
    put_str(&p, end, ") ");
    put_char(&p, end, proc_state_char(t));
    put_char(&p, end, ' ');
    put_dec(&p, end, (u32)t->pid);
    for (int i = 0; i < 36; i++) {
      put_str(&p, end, " 0");
    }
    put_char(&p, end, '\n');
  } else {
    return -1;
  }
  *p = '\0';
  return (int)(p - out);
}

static u32 proc_file_read(vnode_t* node, u32 offset, u32 size, u8* buf) {
  char tmp[256];
  int len = proc_generate(node, tmp, sizeof(tmp));
  if (len < 0) {
    return 0;
  }
  if (offset >= (u32)len) {
    return 0;
  }
  u32 avail = (u32)len - offset;
  u32 n = size < avail ? size : avail;
  kmemcpy(buf, tmp + offset, n);
  return n;
}

static u32 proc_node_close(vnode_t* node) {
  if (node != NULL) {
    if (node->name != NULL) {
      kfree(node->name);
    }
    kfree(node);
  }
  return 0;
}

/* ===================== task 目录枚举 ===================== */

static u32 proc_task_readdir(vnode_t* node, vdirent_t* dirent, u32* offset,
                             u32 count) {
  (void)count;
  int pid = (int)node->impl;
  u32 want = *offset;
  u32 idx = 0;
  /* 进程 = 主线程(t->id==pid) + 其子线程(t->pid==pid)。fork 出的子进程
   * t->pid 也是父 id，会一起列出——wait_for_threads 场景（进程内 pthread、
   * 无 fork）恰好正确。 */
  for (int c = 0; c < MAX_CPU; c++) {
    for (thread_t* t = schedulable_head_thread[c]; t != NULL; t = t->next) {
      if ((int)t->id != pid && (int)t->pid != pid) {
        continue;
      }
      if (idx == want) {
        kmemset(dirent, 0, sizeof(vdirent_t));
        dirent->ino = t->id;
        char* p = dirent->name;
        char* end = dirent->name + sizeof(dirent->name) - 1;
        put_dec(&p, end, t->id);
        *p = '\0';
        dirent->type = V_DIRECTORY;
        *offset = want + 1;
        return 1;
      }
      idx++;
    }
  }
  return 0;
}

/* ===================== 路径解析入口 ===================== */

static vnode_t* proc_make(const char* path, u32 flags, int kind, u32 impl) {
  vnode_t* node = vfs_create_node((u8*)path, flags);
  if (node == NULL) {
    return NULL;
  }
  node->inode = kind;
  node->impl = impl;
  node->op = (flags & V_DIRECTORY) ? &proc_dir_ops : &proc_file_ops;
  return node;
}

vnode_t* procfs_lookup(const char* path) {
  if (kstrncmp(path, "/proc/", 6) != 0) {
    return NULL;
  }
  const char* rest = path + 6;

  if (kstrcmp(rest, "uptime") == 0) {
    return proc_make(path, V_FILE, PROC_UPTIME, 0);
  }
  if (kstrcmp(rest, "sys/kernel/pid_max") == 0) {
    return proc_make(path, V_FILE, PROC_PID_MAX, 0);
  }
  /* /proc/self/... → /proc/<pid>/... 递归一次 */
  if (kstrncmp(rest, "self/", 5) == 0) {
    thread_t* cur = thread_current();
    if (cur == NULL) {
      return NULL;
    }
    char real[96];
    char* p = real;
    put_str(&p, real + sizeof(real) - 1, "/proc/");
    put_dec(&p, real + sizeof(real) - 1, cur->id);
    put_str(&p, real + sizeof(real) - 1, rest + 4);
    *p = '\0';
    return procfs_lookup(real);
  }

  u32 pid = 0;
  int n = parse_dec(rest, &pid);
  if (n == 0) {
    return NULL;
  }
  rest += n;
  if (kstrcmp(rest, "/stat") == 0) {
    return proc_make(path, V_FILE, PROC_STAT_PROC, pid);
  }
  if (kstrcmp(rest, "/task") == 0) {
    return proc_make(path, V_DIRECTORY, PROC_STAT_TID, pid);
  }
  if (kstrncmp(rest, "/task/", 6) == 0) {
    u32 tid = 0;
    int m = parse_dec(rest + 6, &tid);
    if (m == 0) {
      return NULL;
    }
    if (kstrcmp(rest + 6 + m, "/stat") == 0) {
      return proc_make(path, V_FILE, PROC_STAT_TID, tid);
    }
  }
  return NULL;
}
