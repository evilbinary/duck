#ifndef __GAGA_H__
#define __GAGA_H__

#include "kernel/kernel.h"

enum {
   SYS_ADD_CLIENT = 520,
   SYS_DEL_CLIENT = 521,
};

#define MAX_MESSAGES 100


typedef struct api_t{
  char* name;
  void* args;
};


typedef struct message{
  int service_id;
  int type;
  char data[64];
}message_t;

typedef struct client{
  int id;
  int token;
  message_t* message;
}client_t;

#endif