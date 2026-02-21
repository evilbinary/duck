#include "logger.h"

#include "thread.h"

log_t log_info_mod;
char logger_buf[LOG_MSG_BUF];

const char* log_level_strings[] = {
    "debug",  // 0
    "info",   // 1
    "warn",   // 2
    "error",  // 3
};

const char* log_level_color[] = {LOG_CYAN, LOG_GREEN, LOG_YELLOW, LOG_PURPLE};

static size_t log_write(u32 fd, void* buf, size_t nbytes) {
  thread_t* current = thread_current();
  fd_t* f = thread_find_fd_id(current, fd);
  if (f == NULL) {
    log_error("log write not found fd %d tid %d\n", fd, current->id);
    thread_dump_fd(current);
    return 0;
  }
  vnode_t* node = f->data;
  if (node == NULL) {
    log_error("log write node is null tid %d \n", current->id);
    return -1;
  }
  // kprintf("sys write %d %s fd:%s\n",current->id,buf,f->name);
  u32 ret = vwrite(node, f->offset, nbytes, buf);
  f->offset += nbytes;
  return ret;
}


void log_default(int tag, const char* message, va_list args) {
  int ticks = 0;
  int tid = 0;
  // 仅在系统初始化完成后才获取 tick 和线程信息
  if (log_info_mod.logger_size > 0) {
    ticks = schedule_get_ticks();
    thread_t* current = thread_current();
    if (current != NULL) {
      tid = current->id;
    }
  }
  kmemset(logger_buf, 0, LOG_MSG_BUF);
  char* tag_msg = (char*)log_level_strings[tag];
  if (log_info_mod.fd < 0) {
    kprintf("[%08d] tid: %d %s ", ticks, tid, tag_msg);
    kvsprintf(logger_buf, message, args);
    kprintf("%s", logger_buf);
  } else {
    kmemset(logger_buf, 0, LOG_MSG_BUF);
    kvsprintf(logger_buf, "[%08d] tid: %d ", ticks, tid);
    log_write(log_info_mod.fd, logger_buf, kstrlen(logger_buf));
    kmemset(logger_buf, 0, LOG_MSG_BUF);
    kvsprintf(logger_buf, message, args);
    log_write(log_info_mod.fd, logger_buf, kstrlen(logger_buf));
  }
}

void log_default_color(int tag, const char* message, va_list args) {
  int ticks = 0;
  int tid = 0;
  // 仅在系统初始化完成后才获取 tick 和线程信息
  if (log_info_mod.logger_size > 0) {
    ticks = schedule_get_ticks();
    thread_t* current = thread_current();
    if (current != NULL) {
      tid = current->id;
    }
  }
  kmemset(logger_buf, 0, LOG_MSG_BUF);
  char* tag_msg = (char*)log_level_strings[tag];
  char* tag_color = log_level_color[tag];

  if (log_info_mod.fd < 0) {
    // Print header and message separately to avoid nesting logger_buf inside printf_buffer
    kprintf("%s[%08d] %stid:%d %s%-5s %s", LOG_GRAY, ticks,
            LOG_WHITE_BOLD, tid, tag_color, tag_msg, LOG_WHITE);
    int size = kvsprintf(logger_buf, message, args);
    if (size > LOG_MSG_BUF) {
      kprintf("log overflow %d\n", size);
    }
    kprintf("%s%s", logger_buf, LOG_NONE);
  } else {
    kmemset(logger_buf, 0, LOG_MSG_BUF);
    int size =
        kvsprintf(logger_buf, "%s[%08d] %stid:%d %s%-5s %s", LOG_GRAY, ticks,
                 LOG_WHITE_BOLD, tid, tag_color, tag_msg, LOG_NONE);
    if (size > LOG_MSG_BUF) {
      kprintf("log overflow %d\n",size);
    }
    log_write(log_info_mod.fd, logger_buf, kstrlen(logger_buf));
    kmemset(logger_buf, 0, LOG_MSG_BUF);
    size = kvsprintf(logger_buf, message, args);
    if (size > LOG_MSG_BUF) {
      kprintf("log overflow %d\n",size);
    }
    log_write(log_info_mod.fd, logger_buf, kstrlen(logger_buf));
  }
}

void log_format(int tag, const char* message, va_list args) {
  if (tag < LOG_MIN_LEVEL) return;
  // 早期启动阶段：日志系统未初始化，直接回退到 kprintf
  if (log_info_mod.logger_size == 0) {
    kprintf("[early] ");
    char buf[256];
    kvsprintf(buf, message, args);
    kprintf("%s", buf);
    return;
  }
  for (int i = 0; i < log_info_mod.logger_size; i++) {
    log_info_mod.loggers[i](tag, message, args);
  }
}

void log_info(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  log_format(LOG_INFO, fmt, args);
  va_end(args);
}

void log_debug(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  log_format(LOG_DEBUG, fmt, args);
  va_end(args);
}

void log_warn(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  log_format(LOG_WARN, fmt, args);
  va_end(args);
}

void log_error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  log_format(LOG_ERROR, fmt, args);
  va_end(args);
}

void log_register(log_format_fn fn) {
  log_info_mod.loggers[log_info_mod.logger_size++] = fn;
}

void log_unregister(log_format_fn fn) {}

void log_init() {
  log_info_mod.fd = -1;
  log_info_mod.logger_size = 0;
#ifdef LOG_COLOR
  log_register(&log_default_color);
#else
  log_register(&log_default);
#endif
}

void log_init_fd(int fd) { log_info_mod.fd = fd; }