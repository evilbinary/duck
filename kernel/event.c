/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "event.h"

#define DEBUG 1
// #define log_debug

event_t* all_events[MAX_EVENT] = {0};
u32 all_events_count = 0;

void event_poll() {
  // log_debug("process event  %d\n", all_events_count);
  for (int i = 0; i < MAX_EVENT; i++) {
    event_t* e = all_events[i];
    if (e == NULL) {
      continue;
    }
    if (e->source == NULL) {
      continue;
    }
    source_t* source = e->source;
    if (source->type == EVENT_SOURCE_IO) {
      int ret =
          vread(source->node, source->f->offset, source->nbytes, source->buf);
      if (ret > 0) {
#ifdef DEBUG
        thread_dumps();
        log_debug("event read %s offset %d buf %x ret %d\n", e->target->name,
                  source->f->offset, source->buf, ret);
        for (int i = 0; i < ret; i++) {
          kprintf("read=>%x ", source->buf[i]);
        }
        log_debug("event  wakeup %s\n", e->target->name);
#endif
        thread_set_ret(e->target, ret);
        event_wakeup_io(e);
      }
    }
  }
}

void event_init() { all_events_count = 0; }

source_t* event_source_io_create(vnode_t* node, fd_t* f, int nbytes,
                                 char* buf) {
  source_t* source = kmalloc(sizeof(source_t), DEVICE_TYPE);
  void* phy_buf = kpage_v2p(buf, 0);
  source->node = node;
  source->f = f;
  source->nbytes = nbytes;
  source->buf = phy_buf;
  source->type = EVENT_SOURCE_IO;
  return source;
}

void do_event_thread() {
  for (;;) {
    event_poll();
  }
}


void event_wait(thread_t* t, source_t* source) {
  if (t == NULL) {
    return;
  }
  if (source == NULL) {
    return;
  }
  if (source->buf == NULL) {
    return;
  }

  if (t->state == THREAD_WAITING) {
    return;
  }
  log_debug("event wait %s count %d\n", t->name, all_events_count);

  if (all_events_count > MAX_EVENT) {
    log_error("event overflow\n");
    return;
  }
  if (all_events_count <= 0) {
    thread_t* t1 = thread_create_name_level("event", (void*)&do_event_thread,
                                            NULL, LEVEL_KERNEL);
    thread_run(t1);
  }

  event_t* event = kmalloc(sizeof(event_t), DEVICE_TYPE);
  event->source = source;
  event->target = t;

  for (int i = 0; i < MAX_EVENT; i++) {
    if (all_events[i] == NULL) {
      all_events[i] = event;
      all_events_count++;
      break;
    }
  }
  thread_wait(t);
}

void event_wakeup_io(event_t* e) {
  if (all_events_count > 0) {
    thread_wake(e->target);
    for (int i = 0; i < MAX_EVENT; i++) {
      if (all_events[i] == e) {
        all_events[i] = NULL;
        all_events_count--;
        break;
      }
    }
    // free event and source
    // kfree(e->source);
    // kfree(e);
  }
}


