/*******************************************************************
 * 系统配置服务（/conf/system.conf）—— 内核模块 sysconf
 *
 * 【为什么做成模块，且必须注册在 fatfs 之后】
 * 配置在 SD 卡上，而 gpu/lcd 这些驱动的 init 排在 sdhci/fatfs 之前 ——
 * 驱动自己读不到 /conf/*。所以"读配置 → 下发驱动"这件事只能放在文件系统可用
 * 之后，且要在 xwin/用户态开始画之前。做法和 loader（同样要读文件）一致：
 * 在 app/init/module.c 的公共段（在所有平台块之后）注册，
 *     fatfs/fat 之后，xwin 之前
 * 于是 module_run_all() 跑到这里时 "/" 已挂好，日志里能看到
 * "module run sysconf"，顺序一眼可见。
 *
 * 【流程】
 *   module_run_all()
 *     → sysconf_init()
 *       → sysconf_apply(SYSCONF_DEFAULT_PATH)     读文件、解析
 *         → 解析 [lcd] 段
 *           → device_find(DEVICE_VGA)->ioctl(VGA_IOC_SET_LCD_CONF, &conf)
 *             → 各平台 gpu/<plat>.c 的 vga_set_lcd_conf() 重配 DE/TCON
 *
 * 【文件格式】（见 app/resource/conf/system.conf）
 *   # 注释（行首或行尾）
 *   [section]
 *   key = value        value 支持十进制与 0x 十六进制
 * 文件不存在 / 段不存在 / 某一项为 0 ⇒ 保持驱动内置默认值，不报错。
 * 所以没有 SD 卡、没有文件系统的平台（stm32/esp32 等）只是多打一行提示。
 *
 * 【以后加新段】
 *   在 sysconf_apply() 末尾按段追加一个 sysconf_apply_xxx()；
 *   其它内核代码也可以直接用 sysconf_get_int() 取值，不必各自读文件。
 ********************************************************************/
#include "sysconf.h"

#include "kernel/logger.h"
#include "kernel/string.h"
#include "modules/vga/vga.h"

/* 整个文件读进内存再解析：配置文件很小（几百字节），一次读完最省事 */
#define SYSCONF_MAX 4096
#define SYSCONF_LINE 160

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

/* 取 [section] 里 key 的整数值；找不到返回 def */
int sysconf_get_int(const char* section, const char* key, int def) {
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
      return sysconf_parse_num(&line[vs], def);
    }
  }
  return def;
}

/* 把 [lcd] 段下发给 VGA 驱动 */
static void sysconf_apply_lcd(void) {
  lcd_conf_t conf;
  device_t* dev;

  kmemset(&conf, 0, sizeof(conf));
  conf.width = sysconf_get_int("lcd", "width", 0);
  conf.height = sysconf_get_int("lcd", "height", 0);
  conf.bpp = sysconf_get_int("lcd", "bpp", 0);
  conf.pclk_hz = sysconf_get_int("lcd", "pclk_hz", 0);
  conf.hbp = sysconf_get_int("lcd", "hbp", 0);
  conf.hfp = sysconf_get_int("lcd", "hfp", 0);
  conf.hspw = sysconf_get_int("lcd", "hspw", 0);
  conf.vbp = sysconf_get_int("lcd", "vbp", 0);
  conf.vfp = sysconf_get_int("lcd", "vfp", 0);
  conf.vspw = sysconf_get_int("lcd", "vspw", 0);

  if (conf.width == 0 && conf.height == 0 && conf.pclk_hz == 0) {
    log_info("sysconf: no [lcd] section, keep driver defaults\n");
    return;
  }

  dev = device_find(DEVICE_VGA);
  if (dev == NULL) {
    dev = device_find(DEVICE_VGA_QEMU);
  }
  if (dev == NULL || dev->ioctl == NULL) {
    log_warn("sysconf: no vga device, [lcd] skipped\n");
    return;
  }
  if (dev->ioctl(dev, VGA_IOC_SET_LCD_CONF, &conf) == 0) {
    log_warn("sysconf: [lcd] not supported by this driver\n");
    return;
  }
  log_info("sysconf: [lcd] %dx%d bpp=%d pclk=%d applied\n", conf.width,
           conf.height, conf.bpp, conf.pclk_hz);
}

int sysconf_apply(const char* path) {
  if (path == NULL) {
    return -1;
  }
  if (sysconf_load(path) != 0) {
    log_info("sysconf: %s not found, use built-in defaults\n", path);
    return -1;
  }
  log_info("sysconf: load %s (%d bytes)\n", path, sysconf_len);

  sysconf_apply_lcd();
  /* 以后其它子系统的参数（网络/声音/…）也在这里按段追加 */
  return 0;
}

/* ---- 模块入口 ----
 * 由 app/init/module.c 注册在 fatfs 之后（见文件头），init 时自动加载默认路径。
 * 想换路径/运行时重载，直接调 sysconf_apply()。 */
int sysconf_init(void) {
  log_debug("sysconf init\n");
  return sysconf_apply(SYSCONF_DEFAULT_PATH);
}

void sysconf_exit(void) { log_debug("sysconf exit\n"); }

module_t sysconf_module = {
    .name = "sysconf", .init = sysconf_init, .exit = sysconf_exit};
