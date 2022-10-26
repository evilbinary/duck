#ifndef LOGGER_H
#define LOGGER_H

#include "config.h"
#include "kernel/stdarg.h"

#define LOG_MSG_BUF 256
#define LOG_MAX_FN 10

// \033[+背景色+;+字体色+;+效果+m
#define LOG_NONE "\033[m"

#define LOG_WHITE "\033[0;40m"
#define LOG_RED "\033[40;31m"
#define LOG_GREEN "\033[0;32m"
#define LOG_YELLOW "\033[0;33m"
#define LOG_BLUE "\033[0;34m"
#define LOG_PURPLE "\033[0;35m"
#define LOG_CYAN "\033[0;36m"
#define LOG_GRAY "\033[1;37m"

#define LOG_WHITE_BOLD "\033[1;40m"

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