#ifndef LOGGER_H
#define LOGGER_H

#include "kernel/stdarg.h"

#define LOG_MSG_BUF 128

#define LOG_TAG_DEBUG "debug"
#define LOG_TAG_INFO "info"
#define LOG_TAG_WARN "warn"
#define LOG_TAG_ERROR "error"


void log_debug(const char* fmt, ...);

void log_info(const char* fmt, ...);

void log_warn(const char* fmt, ...);

void log_error(const char* fmt, ...);

#endif