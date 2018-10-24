#ifndef _CSIEBOX_SERVER_
#define _CSIEBOX_SERVER_

#ifdef __cplusplus
extern "C" {
#endif

#include "csiebox_common.h"
#include "thread.h"

#include <limits.h>
#include <time.h>

typedef struct {
  char user[USER_LEN_MAX];
  char passwd_hash[MD5_DIGEST_LENGTH];
} csiebox_account_info;

typedef struct {
  csiebox_account_info account;
  int conn_fd;
  time_t time_off;
  // record for first client
  int start;
  // flag for select
  int in_thread;
} csiebox_client_info;

typedef struct {
  struct {
    char path[PATH_MAX];
    char account_path[PATH_MAX];
    int thread;
    char run_path[PATH_MAX];
    int daemonize;
  } arg;
  char fifo[PATH_MAX];
  int listen_fd;
  csiebox_client_info** client;
  thread_pool* threads;
} csiebox_server;

typedef struct {
  csiebox_server* server;
  int conn_fd;
} csiebox_server_thread_arg;

void csiebox_server_init(
  csiebox_server** server, int argc, char** argv);
int csiebox_server_run(csiebox_server* server);
void csiebox_server_destroy(csiebox_server** server);

#ifdef __cplusplus
}
#endif

#endif
