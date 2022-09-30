#include "logger.h"
#include "thread.h"

void log_init() {}

void log_format(const char* tag, const char* message, va_list args) {
  int ticks = schedule_get_ticks();
  int tid = -1;
  thread_t* current = thread_current();
  if (current != NULL) {
    tid = current->id;
  }
  char buf[LOG_MSG_BUF];
  vsprintf(buf, message, args);
  kprintf("[%s] [%s] tid: %d %s\n", ticks, tag, tid, buf);
}

void log_info(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  log_format(LOG_TAG_INFO, fmt, args);
  va_end(args);
}

void log_debug(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  log_format(LOG_TAG_DEBUG, fmt, args);
  va_end(args);
}

void log_warn(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  log_format(LOG_TAG_WARN, fmt, args);
  va_end(args);
}

void log_error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  log_format(LOG_TAG_ERROR, fmt, args);
  va_end(args);
}