#include "logger.h"

#include "thread.h"

log_t log_info_mod;
const char* log_level_strings[] = {
    "debug",  // 0
    "info",   // 1
    "warn",   // 2
    "error",  // 3
};

void log_default(int tag, const char* message, va_list args) {
  int ticks = schedule_get_ticks();
  int tid = -1;
  thread_t* current = thread_current();
  if (current != NULL) {
    tid = current->id;
  }
  char buf[LOG_MSG_BUF] = {0};
  char* tag_msg = (char*)log_level_strings[tag];
  if (log_info_mod.fd < 0) {
    vsprintf(buf, message, args);
    kprintf("[%08d] tid: %d %s  %s", ticks, tid, tag_msg, buf);
  } else {
    kmemset(buf, 0, LOG_MSG_BUF);
    vsprintf(buf, "[%08d] tid: %d ", ticks, tid);
    sys_write(log_info_mod.fd, buf, kstrlen(buf));
    kmemset(buf, 0, LOG_MSG_BUF);
    vsprintf(buf, message, args);
    sys_write(log_info_mod.fd, buf, kstrlen(buf));
  }
}

void log_format(int tag, const char* message, va_list args) {
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
  log_info_mod.logger_size=0;
  log_register(&log_default);
}

void log_init_fd(int fd){
  log_info_mod.fd = fd;
}