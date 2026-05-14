#include "logger.h"

#include "thread.h"

log_t log_info_mod;
char logger_buf[LOG_MSG_BUF];
static int log_runtime_ready = 0;

typedef struct log_runtime_state {
  int ready;
  int ticks;
  int tid;
  thread_t* current;
} log_runtime_state_t;

static void log_emit_line(const char* line) {
  if (line == NULL) {
    return;
  }
  kprintf(line);
}

static log_runtime_state_t log_get_runtime_state(void) {
  log_runtime_state_t state;
  kmemset(&state, 0, sizeof(state));
  if (log_info_mod.logger_size == 0) {
    return state;
  }
  if (!log_runtime_ready) {
    return state;
  }
  state.current = thread_current();
  if (state.current == NULL || state.current->ctx == NULL) {
    return state;
  }
  state.ready = 1;
  state.ticks = schedule_get_ticks();
  state.tid = state.current->id;
  return state;
}

const char* log_level_strings[] = {
    "debug",  // 0
    "info",   // 1
    "warn",   // 2
    "error",  // 3
};

const char* log_level_color[] = {LOG_CYAN, LOG_GREEN, LOG_YELLOW, LOG_PURPLE};

static void log_emit_early_line(int tag, const char* message, va_list args) {
  char buf[384];
  kmemset(buf, 0, sizeof(buf));
  int off = 0;
  static const char early_level_prefix[] = {'D', 'I', 'W', 'E'};
  if (tag >= LOG_DEBUG && tag <= LOG_ERROR) {
    buf[off++] = early_level_prefix[tag];
    buf[off++] = ':';
  }
  kvsprintf(buf + off, message, args);
  log_emit_line(buf);
}

static size_t log_write(log_runtime_state_t* state, u32 fd, void* buf, size_t nbytes) {
  if (state == NULL || !state->ready || state->current == NULL) {
    return 0;
  }
  fd_t* f = thread_find_fd_id(state->current, fd);
  if (f == NULL || f->data == NULL) {
    return 0;
  }
  vnode_t* node = f->data;
  u32 ret = vwrite(node, f->offset, nbytes, buf);
  f->offset += nbytes;
  return ret;
}

static void log_default_runtime(log_runtime_state_t* state, int tag,
                                const char* message, va_list args) {
  char line_buf[LOG_MSG_BUF * 2];
  kmemset(logger_buf, 0, LOG_MSG_BUF);
  kmemset(line_buf, 0, sizeof(line_buf));
  char* tag_msg = (char*)log_level_strings[tag];
  if (log_info_mod.fd < 0) {
    int off = kvsprintf(line_buf, "[%08d] tid: %d %s ", state->ticks, state->tid,
                        tag_msg);
    kvsprintf(line_buf + off, message, args);
    log_emit_line(line_buf);
  } else {
    kmemset(logger_buf, 0, LOG_MSG_BUF);
    kvsprintf(logger_buf, "[%08d] tid: %d ", state->ticks, state->tid);
    log_write(state, log_info_mod.fd, logger_buf, kstrlen(logger_buf));
    kmemset(logger_buf, 0, LOG_MSG_BUF);
    kvsprintf(logger_buf, message, args);
    log_write(state, log_info_mod.fd, logger_buf, kstrlen(logger_buf));
  }
}

void log_default(int tag, const char* message, va_list args) {
  log_runtime_state_t state = log_get_runtime_state();
  if (!state.ready) {
    log_emit_early_line(tag, message, args);
    return;
  }
  log_default_runtime(&state, tag, message, args);
}

static void log_default_color_runtime(log_runtime_state_t* state, int tag,
                                      const char* message, va_list args) {
  char line_buf[LOG_MSG_BUF * 2];
  kmemset(logger_buf, 0, LOG_MSG_BUF);
  kmemset(line_buf, 0, sizeof(line_buf));
  char* tag_msg = (char*)log_level_strings[tag];
  char* tag_color = log_level_color[tag];

  if (log_info_mod.fd < 0) {
    int off = kvsprintf(line_buf, "%s[%08d] %stid:%d %s%-5s %s", LOG_GRAY,
                        state->ticks, LOG_WHITE_BOLD, state->tid, tag_color,
                        tag_msg, LOG_WHITE);
    int size = kvsprintf(line_buf + off, message, args);
    if (size > LOG_MSG_BUF) {
      kprintf("log overflow %d\n", size);
    }
    kvsprintf(line_buf + off + size, "%s", LOG_NONE);
    log_emit_line(line_buf);
  } else {
    kmemset(logger_buf, 0, LOG_MSG_BUF);
    int size =
        kvsprintf(logger_buf, "%s[%08d] %stid:%d %s%-5s %s", LOG_GRAY,
                 state->ticks, LOG_WHITE_BOLD, state->tid, tag_color, tag_msg,
                 LOG_NONE);
    if (size > LOG_MSG_BUF) {
      kprintf("log overflow %d\n",size);
    }
    log_write(state, log_info_mod.fd, logger_buf, kstrlen(logger_buf));
    kmemset(logger_buf, 0, LOG_MSG_BUF);
    size = kvsprintf(logger_buf, message, args);
    if (size > LOG_MSG_BUF) {
      kprintf("log overflow %d\n",size);
    }
    log_write(state, log_info_mod.fd, logger_buf, kstrlen(logger_buf));
  }
}

void log_default_color(int tag, const char* message, va_list args) {
  log_runtime_state_t state = log_get_runtime_state();
  if (!state.ready) {
    log_emit_early_line(tag, message, args);
    return;
  }
  log_default_color_runtime(&state, tag, message, args);
}

void log_format(int tag, const char* message, va_list args) {
  if (tag < LOG_MIN_LEVEL) return;
  log_runtime_state_t state = log_get_runtime_state();
  if (!state.ready) {
    log_emit_early_line(tag, message, args);
    return;
  }
  for (int i = 0; i < log_info_mod.logger_size; i++) {
    va_list logger_args;
    va_copy(logger_args, args);
    log_info_mod.loggers[i](tag, message, logger_args);
    va_end(logger_args);
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
  log_runtime_ready = 0;
#ifdef LOG_COLOR
  log_register(&log_default_color);
#else
  log_register(&log_default);
#endif
}

void log_set_runtime_ready(int ready) { log_runtime_ready = ready; }

int log_is_runtime_ready(void) { return log_runtime_ready; }

void log_init_fd(int fd) { log_info_mod.fd = fd; }