#ifndef LOGGER_H
#define LOGGER_H

#include "kernel/stdarg.h"

#define LOG_MSG_BUF 128
#define LOG_MAX_FN 10

enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

typedef void (*log_format_fn)(int tag, const char* message, va_list args);

typedef struct log {
  int fd;
  log_format_fn loggers[LOG_MAX_FN];
  int logger_size;
} log_t;

void log_debug(const char* fmt, ...);

void log_info(const char* fmt, ...);

void log_warn(const char* fmt, ...);

void log_error(const char* fmt, ...);

#endif