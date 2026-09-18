/*******************************************************************
 * 系统配置服务（/conf/system.conf）—— 内核模块 sysconf
 *
 * 【职责：只做三件事，不认识任何具体驱动/设备】
 *   1) 读文件并解析（段/键/值，格式见下）；
 *   2) 对外提供取值：sysconf_get_int() / sysconf_get_str()；
 *   3) 文件系统就绪后，按注册顺序回调各驱动登记的"配置就绪"函数
 *      （sysconf_on_ready()）——取值和写硬件都在驱动自己那边。
 *
 * 【为什么必须做成模块、且注册在 fatfs 之后】
 * 配置在 SD 卡上，而 gpu/lcd 这些驱动的 init 排在 sdhci/fatfs 之前（见
 * app/init/module.c 的注册顺序）——驱动 init 时 "/" 还没挂，自己读不到文件。
 * 所以"什么时候能读文件"只能由这个模块提供：做法和 loader（同样要读文件）
 * 一致，注册在公共段（所有平台块之后）：fatfs/fat 之后、xwin 之前。
 * 驱动要参与配置，只需在自己的 init 里 sysconf_on_ready(自己的应用函数)，
 * 登记不读文件、时机无所谓。
 *
 * 【流程】
 *   module_run_all()
 *     → gpu/lcd/… 的 init：… sysconf_on_ready(v3s_lcd_apply_conf) …
 *     → fatfs init："/" 挂好
 *     → sysconf_init()
 *       → sysconf_apply(SYSCONF_DEFAULT_PATH)     读文件、解析
 *         → 按登记顺序回调（drivers 在这里 sysconf_get_* 取值并重配硬件）
 *
 * 【文件格式】（见 app/resource/conf/system.conf）
 *   # 注释（行首或行尾）
 *   [section]
 *   key = value        value 支持十进制与 0x 十六进制（负数也可）
 * 文件不存在 / 段不存在 / 键不存在 ⇒ 取值接口返回调用方给的 def，驱动据此
 * 保持内置默认，不报错；没有 SD 卡/文件系统的平台只会多打一行提示。
 ********************************************************************/
#include "sysconf.h"

#include "kernel/logger.h"
#include "kernel/string.h"

/* 整个文件读进内存再解析：配置文件很小（几百字节），一次读完最省事 */
#define SYSCONF_MAX 4096
#define SYSCONF_LINE 160

/* 能登记多少个"配置就绪"回调（驱动数量级，够用即可） */
#ifndef SYSCONF_MAX_APPLY
#define SYSCONF_MAX_APPLY 8
#endif

static char sysconf_buf[SYSCONF_MAX + 1];
static u32 sysconf_len = 0;
static int sysconf_ok = 0;

/* 读文件到 sysconf_buf；成功返回 0 */
static int sysconf_load(const char* path) {
  sysconf_len = 0;
  sysconf_ok = 0;
  sysconf_buf[0] = 0;

  /* 没有文件系统（root_node 为空）时 vfs_find 直接返回 NULL，这里天然安全 */
  vnode_t* node = vfs_find(NULL, path);
  if (node == NULL || node->op == NULL || node->op->read == NULL) {
    return -1;
  }
  if (node->op->open != NULL) {
    node->op->open(node, 0);
  }
  u32 len = node->length;
  if (len == 0 || len > SYSCONF_MAX) {
    len = SYSCONF_MAX;
  }
  u32 got = node->op->read(node, 0, len, (u8*)sysconf_buf);
  if (got == 0 || got == (u32)-1) {
    return -1;
  }
  if (got > SYSCONF_MAX) {
    got = SYSCONF_MAX;
  }
  if (node->op->close != NULL) {
    node->op->close(node);
  }
  sysconf_len = got;
  sysconf_buf[sysconf_len] = 0;
  sysconf_ok = 1;
  return 0;
}

/* 解析整数：十进制 / 0x 十六进制；没有数字则返回 def */
static int sysconf_parse_num(const char* p, int def) {
  int v = 0;
  int neg = 0;
  int any = 0;

  while (*p == ' ' || *p == '\t') {
    p++;
  }
  if (*p == '-') {
    neg = 1;
    p++;
  }
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    p += 2;
    for (;; p++) {
      char c = *p;
      int d;
      if (c >= '0' && c <= '9') {
        d = c - '0';
      } else if (c >= 'a' && c <= 'f') {
        d = c - 'a' + 10;
      } else if (c >= 'A' && c <= 'F') {
        d = c - 'A' + 10;
      } else {
        break;
      }
      v = (v << 4) + d;
      any = 1;
    }
  } else {
    while (*p >= '0' && *p <= '9') {
      v = v * 10 + (*p - '0');
      any = 1;
      p++;
    }
  }
  if (!any) {
    return def;
  }
  return neg ? -v : v;
}

int sysconf_loaded(void) { return sysconf_ok; }

/* 【按 段+键 取原始值】把 [section] 里 key 的 value 拷进 out（已去注释、去首尾
 * 空白）。找到返回 1，没找到返回 0。整型/字符串两个取用接口都建在它上面。 */
static int sysconf_find(const char* section, const char* key, char* out,
                        u32 out_size) {
  char line[SYSCONF_LINE];
  char cur_section[32];
  int sec_ok = 0;
  u32 i = 0;
  u32 want = kstrlen(key);
  u32 want_sec = kstrlen(section);

  cur_section[0] = 0;

  while (i < sysconf_len) {
    int n = 0;
    /* 取一行 */
    while (i < sysconf_len && sysconf_buf[i] != '\n' &&
           n < (int)sizeof(line) - 1) {
      line[n++] = sysconf_buf[i++];
    }
    while (i < sysconf_len && sysconf_buf[i] != '\n') {
      i++; /* 超长行：丢掉多余部分 */
    }
    if (i < sysconf_len) {
      i++; /* 跳过 '\n' */
    }
    line[n] = 0;

    /* 行内注释 */
    for (int j = 0; j < n; j++) {
      if (line[j] == '#') {
        n = j;
        line[n] = 0;
        break;
      }
    }
    /* 去首尾空白 */
    int s = 0;
    while (s < n && (line[s] == ' ' || line[s] == '\t')) {
      s++;
    }
    while (n > s && (line[n - 1] == ' ' || line[n - 1] == '\t' ||
                     line[n - 1] == '\r')) {
      line[--n] = 0;
    }
    if (n <= s) {
      continue;
    }

    if (line[s] == '[') {
      /* 段名 */
      int e = s + 1;
      int len;
      while (e < n && line[e] != ']') {
        e++;
      }
      len = e - s - 1;
      if (len < 0) {
        len = 0;
      }
      if (len > (int)sizeof(cur_section) - 1) {
        len = sizeof(cur_section) - 1;
      }
      kstrncpy(cur_section, &line[s + 1], len);
      cur_section[len] = 0;
      sec_ok = (kstrlen(cur_section) == want_sec &&
                kstrncmp(cur_section, section, want_sec) == 0);
      continue;
    }

    if (!sec_ok) {
      continue;
    }

    /* key = value */
    {
      int e = s;
      int ks = s;
      int ke;
      int vs;
      while (e < n && line[e] != '=') {
        e++;
      }
      if (e >= n) {
        continue;
      }
      ke = e;
      while (ke > ks && (line[ke - 1] == ' ' || line[ke - 1] == '\t')) {
        ke--;
      }
      if ((u32)(ke - ks) != want || kstrncmp(&line[ks], key, want) != 0) {
        continue;
      }
      vs = e + 1;
      while (vs < n && (line[vs] == ' ' || line[vs] == '\t')) {
        vs++;
      }
      /* 拷出值：line 已 NUL 结尾，注释与首尾空白前面都处理过了 */
      {
        u32 len = (u32)(n - vs);
        if (len > out_size - 1) {
          len = out_size - 1;
        }
        kstrncpy(out, &line[vs], len);
        out[len] = 0;
      }
      return 1;
    }
  }
  out[0] = 0;
  return 0;
}

/* 取 [section] 里 key 的整数值；找不到返回 def */
int sysconf_get_int(const char* section, const char* key, int def) {
  char v[SYSCONF_LINE];

  if (!sysconf_find(section, key, v, sizeof(v))) {
    return def;
  }
  return sysconf_parse_num(v, def);
}

/* 取 [section] 里 key 的字符串值；没取到返回 -1 且 out 置空 */
int sysconf_get_str(const char* section, const char* key, char* out,
                    u32 out_size) {
  if (out == NULL || out_size == 0) {
    return -1;
  }
  out[0] = 0;
  if (!sysconf_find(section, key, out, out_size)) {
    return -1;
  }
  return 0;
}

/* ---------------- 配置就绪回调 ---------------- */

static sysconf_apply_fn sysconf_apply_list[SYSCONF_MAX_APPLY];
static int sysconf_apply_count = 0;

void sysconf_on_ready(sysconf_apply_fn fn) {
  if (fn == NULL) {
    return;
  }
  if (sysconf_apply_count >= SYSCONF_MAX_APPLY) {
    log_warn("sysconf: too many apply hooks (%d)\n", sysconf_apply_count);
    return;
  }
  sysconf_apply_list[sysconf_apply_count++] = fn;
  log_debug("sysconf: apply hook %d registered\n", sysconf_apply_count);
}

static void sysconf_notify_ready(void) {
  int i;
  for (i = 0; i < sysconf_apply_count; i++) {
    sysconf_apply_list[i]();
  }
}

int sysconf_apply(const char* path) {
  if (path == NULL) {
    return -1;
  }
  if (sysconf_load(path) != 0) {
    log_info("sysconf: %s not found, use built-in defaults\n", path);
    sysconf_notify_ready(); /* 也让驱动知道"没有配置"，便于统一处理 */
    return -1;
  }
  log_info("sysconf: load %s (%d bytes)\n", path, sysconf_len);

  sysconf_notify_ready();
  return 0;
}

/* ---------------- 模块入口 ----------------
 * 注册在 fatfs 之后（见 app/init/module.c），此时 "/" 已挂好。 */
int sysconf_init(void) {
  log_debug("sysconf init\n");
  return sysconf_apply(SYSCONF_DEFAULT_PATH);
}

void sysconf_exit(void) { log_debug("sysconf exit\n"); }

module_t sysconf_module = {
    .name = "sysconf", .init = sysconf_init, .exit = sysconf_exit};
