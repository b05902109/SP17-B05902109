#include "csiebox_server.h"

#include "csiebox_common.h"
#include "connect.h"
#include "thread.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <utime.h>
#include <time.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>

static int parse_arg(csiebox_server* server, int argc, char** argv);
static void handle_request(csiebox_server* server, int conn_fd);
static void choose_thread(csiebox_server* server, int conn_fd);
static void server_thread_func(void* input, void* output);
static int get_account_info(csiebox_server* server,  const char* user, csiebox_account_info* info);
static void login(csiebox_server* server, int conn_fd, csiebox_protocol_login* login);
static void logout(csiebox_server* server, int conn_fd);
static void sync_file(csiebox_server* server, int conn_fd, csiebox_protocol_meta* meta);
static char* get_user_homedir(csiebox_server* server, csiebox_client_info* info);
static char* check_client_and_get_homedir(csiebox_server* server, int conn_fd, int client_id);
static void rm_file(csiebox_server* server, int conn_fd, csiebox_protocol_rm* rm);
char fifo_name[5000];
int fifo_fd;
uint32_t active_thread_num = 0;
void sighandler(int signum){
  if(signum == SIGUSR1){
    //fprintf(stderr, "get SIGUS1\n");
    uint32_t temp = htonl(active_thread_num);
    write(fifo_fd, &temp, sizeof(uint32_t));
  }
  else if(signum == SIGINT || signum == SIGTERM){
    //fprintf(stderr, "get SIGINT or SIGTERM\n");
    close(fifo_fd);
    unlink(fifo_name);
    exit(1);
  }
  else{
    //fprintf(stderr, "unknow SIG\n");
  }
  return;
}
void csiebox_server_init(csiebox_server** server, int argc, char** argv) {
  csiebox_server* tmp = (csiebox_server*)malloc(sizeof(csiebox_server));
  if (!tmp) {
    fprintf(stderr, "server malloc fail\n");
    return;
  }
  memset(tmp, 0, sizeof(csiebox_server));
  if (!parse_arg(tmp, argc, argv)) {
    fprintf(stderr, "Usage: %s [config file] [-d]\n", argv[0]);
    free(tmp);
    return;
  }

  int fd = server_start();
  if (fd < 0) {
    fprintf(stderr, "server fail\n");
    free(tmp);
    return;
  }
  tmp->client = (csiebox_client_info**)malloc(sizeof(csiebox_client_info*) * getdtablesize());
  if (!tmp->client) {
    fprintf(stderr, "client list malloc fail\n");
    close(fd);
    free(tmp);
    return;
  }
  memset(tmp->client, 0, sizeof(csiebox_client_info*) * getdtablesize());
  tmp->listen_fd = fd;
  *server = tmp;

  if(tmp->arg.daemonize == 1){
    int i;
    pid_t pid;
    if ( (pid = fork()) != 0)
        exit(0);            /* parent terminates */
    /* 1st child continues */
    setsid();               /* become session leader */
    signal(SIGHUP, SIG_IGN);
    if ( (pid = fork()) != 0)
        exit(0);            /* 1st child terminates */
    /* 2nd child continues */
    char pidfile_name[5000], current_pid_str[10];
    sprintf(pidfile_name, "%s/csiebox_server.pid", tmp->arg.run_path);
    fprintf(stderr, "-> %s!!!", pidfile_name);
    sprintf(current_pid_str, "%d", getpid());
    fprintf(stderr, "pid is %s\n", current_pid_str);
    
    int pidfile_fd = open(pidfile_name, O_WRONLY|O_TRUNC);
    if(pidfile_fd<0) fprintf(stderr, "pidfile open fail\n");
    write(pidfile_fd, current_pid_str, strlen(current_pid_str));
    close(pidfile_fd);
    
    umask(0);               /* clear our file mode creation mask */
  }
}

int csiebox_server_run(csiebox_server* server) {
  init_thread_pool(&(server->threads), server->arg.thread);
  int conn_fd, conn_len;
  struct sockaddr_in addr;

  fd_set readset;
  int maxfd = 1024;
  int i = 0;

  sprintf(fifo_name, "%s/fifo.%d", (server->arg.run_path), getpid());
  fprintf(stderr, "pid is %d, fifo_name is %s\n", getpid(), fifo_name);
  signal(SIGUSR1, sighandler);
  signal(SIGINT, sighandler);
  signal(SIGTERM, sighandler);
  if(mkfifo(fifo_name, 0666)<0) fprintf(stderr, "mkfifo fail\n");
  fifo_fd = open(fifo_name, O_RDWR);
  if(fifo_fd < 0)  fprintf(stderr, "open fifo fail\n");

  struct timeval timeout;
  while (1) {
    FD_ZERO(&readset);
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    int count = 0;
    for (i = 2; i < maxfd; ++i) {
      if (server->client[i] && !(server->client[i]->in_thread)) {
        FD_SET(i, &readset);
      }
      if (server->client[i] && (server->client[i]->in_thread)) {
        count++;
      }
    }
    active_thread_num = count;
    FD_SET(server->listen_fd, &readset);
    if (select(maxfd, &readset, NULL, NULL, &timeout) < 0) {
      if (errno == EINTR){
        continue;
      }
      else{
        fprintf(stderr, "select error\n");
      }
      break;
    }
    if(FD_ISSET(server->listen_fd, &readset)){
      memset(&addr, 0, sizeof(addr));
      conn_len = 0;
      // waiting client connect
      conn_fd = accept(server->listen_fd, (struct sockaddr*)&addr, (socklen_t*)&conn_len);
      if (conn_fd < 0){
        if(errno == ENFILE){
          fprintf(stderr, "out of file descriptor table\n");
          continue;
        }
        else if(errno == EAGAIN || errno == EINTR){
          continue;
        }
        else{
          fprintf(stderr, "accept err\n");
          fprintf(stderr, "code: %s\n", strerror(errno));
          break;
        }
      }
      // handle request from connected socket fd

      handle_request(server, conn_fd);
    }
    else {
      for(i = 2; i < maxfd; ++i){
        if(FD_ISSET(i, &readset)){
          choose_thread(server, i);
        }
      }
    }
  }
  return 1;
}

void csiebox_server_destroy(csiebox_server** server) {
  csiebox_server* tmp = *server;
  *server = 0;
  if (!tmp) {
    return;
  }
  destroy_thread_pool(&(tmp->threads));
  close(tmp->listen_fd);
  int i = 1023;
  for (; i >= 0; --i) {
    if (tmp->client[i]) {
      free(tmp->client[i]);
    }
  }
  free(tmp->client);
  free(tmp);
}

static int parse_arg(csiebox_server* server, int argc, char** argv) {
  if (argc < 2) {
    return 0;
  }
  else{
    int i;
    for(i=1;i<argc;i++)
      if(strcmp(argv[i], "-d")==0){
        server -> arg.daemonize = 1;
        break;
      }
  }
  FILE* file = fopen(argv[1], "r");
  if (!file) {
    return 0;
  }
  fprintf(stderr, "reading config...\n");
  size_t keysize = 20, valsize = 20;
  char* key = (char*)malloc(sizeof(char) * keysize);
  char* val = (char*)malloc(sizeof(char) * valsize);
  ssize_t keylen, vallen;
  int accept_config_total = 3;
  int accept_config[3] = {0, 0, 0};
  while ((keylen = getdelim(&key, &keysize, '=', file) - 1) > 0) {
    key[keylen] = '\0';
    vallen = getline(&val, &valsize, file) - 1;
    val[vallen] = '\0';
    fprintf(stderr, "config (%zd, %s)=(%zd, %s)\n", keylen, key, vallen, val);
    if (strcmp("path", key) == 0) {
      if (vallen <= sizeof(server->arg.path)) {
        strncpy(server->arg.path, val, vallen);
        accept_config[0] = 1;
      }
    } else if (strcmp("account_path", key) == 0) {
      if (vallen <= sizeof(server->arg.account_path)) {
        strncpy(server->arg.account_path, val, vallen);
        accept_config[1] = 1;
      }
    } else if (strcmp("thread", key) == 0) {
      server->arg.thread = atoi(val);
      accept_config[2] = 1;
    } else if (strcmp("run_path", key) == 0) {
      if (vallen <= sizeof(server->arg.account_path)) {
        strncpy(server->arg.run_path, val, vallen);
      }
    }
  }
  free(key);
  free(val);
  fclose(file);
  int i, test = 1;
  for (i = 0; i < accept_config_total; ++i) {
    test = test & accept_config[i];
  }
  if (!test) {
    fprintf(stderr, "config error\n");
    return 0;
  }
  return 1;
}

static void handle_request(csiebox_server* server, int conn_fd) {
  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  if (!recv_message(conn_fd, &header, sizeof(header))) {
    fprintf(stderr, "end of connection\n");
    logout(server, conn_fd);
    return;
  }
  if (header.req.magic != CSIEBOX_PROTOCOL_MAGIC_REQ) {
    return;
  }
  switch (header.req.op) {
    case CSIEBOX_PROTOCOL_OP_LOGIN:
      fprintf(stderr, "login\n");
      csiebox_protocol_login req;
      if (complete_message_with_header(conn_fd, &header, &req)) {
        login(server, conn_fd, &req);
      }
      break;
    case CSIEBOX_PROTOCOL_OP_SYNC_META:
      fprintf(stderr, "sync meta\n");
      csiebox_protocol_meta meta;
      if (complete_message_with_header(conn_fd, &header, &meta)) {
        sync_file(server, conn_fd, &meta);
      }
      break;
    case CSIEBOX_PROTOCOL_OP_SYNC_END:
      fprintf(stderr, "sync end\n");
      break;
    case CSIEBOX_PROTOCOL_OP_RM:
      fprintf(stderr, "rm\n");
      csiebox_protocol_rm rm;
      if (complete_message_with_header(conn_fd, &header, &rm)) {
        rm_file(server, conn_fd, &rm);
      }
      break;
    default:
      fprintf(stderr, "unknow op %x\n", header.req.op);
      break;
  }
}

static void choose_thread(csiebox_server* server, int conn_fd) {
  task_thread_arg task_arg;
  memset(&task_arg, 0, sizeof(task_arg));
  csiebox_server_thread_arg* arg =(csiebox_server_thread_arg*)malloc(sizeof(csiebox_server_thread_arg));
  memset(arg, 0, sizeof(csiebox_server_thread_arg));
  arg->server = server;
  arg->conn_fd = conn_fd;

  task_arg.input = arg;
  task_arg.func = server_thread_func;

  server->client[conn_fd]->in_thread = 1;
  if (!run_task(server->threads, &task_arg)) {
    server->client[conn_fd]->in_thread = 0;
    csiebox_protocol_header header;
    memset(&header, 0, sizeof(header));
    if (!recv_message(conn_fd, &header, sizeof(header))) {
      fprintf(stderr, "end of connection\n");
      logout(server, conn_fd);
      return;
    }
    char* buf = (char*)malloc(sizeof(char) * header.req.datalen);
    recv_message(conn_fd, buf, header.req.datalen);
    free(buf);
    header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
    header.res.datalen = 0;
    header.res.status = CSIEBOX_PROTOCOL_STATUS_BUSY;
    header.res.client_id = conn_fd;
    send_message(conn_fd, &header, sizeof(header));
  }
}

static void server_thread_func(void* input, void* output) {
  csiebox_server_thread_arg* arg = (csiebox_server_thread_arg*)input;
  csiebox_server* server = arg->server;
  int conn_fd = arg->conn_fd;
  free(arg);
  handle_request(server, conn_fd);
  if (server->client[conn_fd]) {
    server->client[conn_fd]->in_thread = 0;
  }
}

static int get_account_info(
  csiebox_server* server,  const char* user, csiebox_account_info* info) {
  FILE* file = fopen(server->arg.account_path, "r");
  if (!file) {
    return 0;
  }
  size_t buflen = 100;
  char* buf = (char*)malloc(sizeof(char) * buflen);
  memset(buf, 0, buflen);
  ssize_t len;
  int ret = 0;
  int line = 0;
  while ((len = getline(&buf, &buflen, file) - 1) > 0) {
    ++line;
    buf[len] = '\0';
    char* u = strtok(buf, ",");
    if (!u) {
      fprintf(stderr, "ill form in account file, line %d\n", line);
      continue;
    }
    if (strcmp(user, u) == 0) {
      memcpy(info->user, user, strlen(user));
      char* passwd = strtok(NULL, ",");
      if (!passwd) {
        fprintf(stderr, "ill form in account file, line %d\n", line);
        continue;
      }
      md5(passwd, strlen(passwd), info->passwd_hash);
      ret = 1;
      break;
    }
  }
  free(buf);
  fclose(file);
  return ret;
}

static void login(csiebox_server* server, int conn_fd, csiebox_protocol_login* login) {
  int succ = 1;
  csiebox_client_info* info =
    (csiebox_client_info*)malloc(sizeof(csiebox_client_info));
  memset(info, 0, sizeof(csiebox_client_info));
  if (!get_account_info(server, login->message.body.user, &(info->account))) {
    fprintf(stderr, "cannot find account\n");
    succ = 0;
  }
  if (succ &&
      memcmp(login->message.body.passwd_hash,
             info->account.passwd_hash,
             MD5_DIGEST_LENGTH) != 0) {
    fprintf(stderr, "passwd miss match\n");
    succ = 0;
  }

  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
  header.res.op = CSIEBOX_PROTOCOL_OP_LOGIN;
  header.res.datalen = 0;
  if (succ) {
    if (server->client[conn_fd]) {
      free(server->client[conn_fd]);
    }
    info->conn_fd = conn_fd;
    server->client[conn_fd] = info;
    header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
    header.res.client_id = info->conn_fd;
    char* homedir = get_user_homedir(server, info);
    mkdir(homedir, DIR_S_FLAG);
    free(homedir);
  } else {
    header.res.status = CSIEBOX_PROTOCOL_STATUS_FAIL;
    free(info);
  }
  send_message(conn_fd, &header, sizeof(header));
}

static void logout(csiebox_server* server, int conn_fd) {
  free(server->client[conn_fd]);
  server->client[conn_fd] = 0;
  close(conn_fd);
}

static void sync_file(csiebox_server* server, int conn_fd, csiebox_protocol_meta* meta) {
  char* homedir = check_client_and_get_homedir(server, conn_fd, meta->message.header.req.client_id);
  if (!homedir) {
    return;
  }
  char buf[PATH_MAX], req_path[PATH_MAX];
  memset(buf, 0, PATH_MAX);
  memset(req_path, 0, PATH_MAX);
  recv_message(conn_fd, buf, meta->message.body.pathlen);
  sprintf(req_path, "%s%s", homedir, buf);
  fprintf(stderr, "req_path: %s\n", req_path);
  struct stat stat;
  memset(&stat, 0, sizeof(struct stat));
  int need_data = 0, change = 0;
  if (lstat(req_path, &stat) < 0) {
    need_data = 1;
    change = 1;
  } else { 					
    if(stat.st_mode != meta->message.body.stat.st_mode) { 
      chmod(req_path, meta->message.body.stat.st_mode);
    }				
    if(stat.st_atime != meta->message.body.stat.st_atime ||
       stat.st_mtime != meta->message.body.stat.st_mtime){
      struct utimbuf* buf = (struct utimbuf*)malloc(sizeof(struct utimbuf));
      buf->actime = meta->message.body.stat.st_atime;
      buf->modtime = meta->message.body.stat.st_mtime;
      if(utime(req_path, buf)!=0){
        printf("time fail\n");
      }
    }
    uint8_t hash[MD5_DIGEST_LENGTH];
    memset(hash, 0, MD5_DIGEST_LENGTH);
    if ((stat.st_mode & S_IFMT) == S_IFDIR) {
    } else {
      md5_file(req_path, hash);
    }
    if (memcmp(hash, meta->message.body.hash, MD5_DIGEST_LENGTH) != 0) {
      need_data = 1;
    }
  }
  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
  header.res.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
  header.res.datalen = 0;
  header.res.client_id = conn_fd;
  if (need_data) {
    header.res.status = CSIEBOX_PROTOCOL_STATUS_MORE;
  } else {
    header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
  }
  send_message(conn_fd, &header, sizeof(header));
  
  if (need_data) {
    csiebox_protocol_file file;
    memset(&file, 0, sizeof(file));
    recv_message(conn_fd, &file, sizeof(file));
    fprintf(stderr, "sync file: %zd\n", file.message.body.datalen);
    if ((meta->message.body.stat.st_mode & S_IFMT) == S_IFDIR) {
      fprintf(stderr, "dir\n");
      mkdir(req_path, DIR_S_FLAG);
    } else {
      fprintf(stderr, "regular file\n");
      int fd = open(req_path, O_CREAT | O_WRONLY | O_TRUNC, REG_S_FLAG);
      size_t total = 0, readlen = 0;;
      char buf[4096];
      memset(buf, 0, 4096);
      while (file.message.body.datalen > total) {
        if (file.message.body.datalen - total < 4096) {
          readlen = file.message.body.datalen - total;
        } else {
          readlen = 4096;
        }
        if (!recv_message(conn_fd, buf, readlen)) {
          fprintf(stderr, "file broken\n");
          break;
        }
        total += readlen;
        if (fd > 0) {
          write(fd, buf, readlen);
        }
      }
      if (fd > 0) {
        close(fd);
      }
    }
    if (change) {
      chmod(req_path, meta->message.body.stat.st_mode);
      struct utimbuf* buf = (struct utimbuf*)malloc(sizeof(struct utimbuf));
      buf->actime = meta->message.body.stat.st_atime;
      buf->modtime = meta->message.body.stat.st_mtime;
      utime(req_path, buf);
    }
    header.res.op = CSIEBOX_PROTOCOL_OP_SYNC_FILE;
    header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
    send_message(conn_fd, &header, sizeof(header));
  }
  free(homedir);
}

static char* get_user_homedir(
  csiebox_server* server, csiebox_client_info* info) {
  char* ret = (char*)malloc(sizeof(char) * PATH_MAX);
  memset(ret, 0, PATH_MAX);
  sprintf(ret, "%s/%s", server->arg.path, info->account.user);
  return ret;
}

static char* check_client_and_get_homedir(
  csiebox_server* server, int conn_fd, int client_id) {
  csiebox_client_info* info = server->client[conn_fd];
  if (!info || conn_fd != client_id) {
    fprintf(stderr, "invalid client: %d\n", conn_fd);
    return NULL;
  }
  return get_user_homedir(server, info);
}

static void rm_file(
  csiebox_server* server, int conn_fd, csiebox_protocol_rm* rm) {
  char* homedir = check_client_and_get_homedir(server, conn_fd, rm->message.header.req.client_id);
  if (!homedir) {
    return;
  }
  char req_path[PATH_MAX], buf[PATH_MAX];
  memset(req_path, 0, PATH_MAX);
  memset(buf, 0, PATH_MAX);
  recv_message(conn_fd, buf, rm->message.body.pathlen);
  sprintf(req_path, "%s%s", homedir, buf);
  free(homedir);
  fprintf(stderr, "rm (%zd, %s)\n", strlen(req_path), req_path);
  struct stat stat;
  memset(&stat, 0, sizeof(stat));
  lstat(req_path, &stat);
  if ((stat.st_mode & S_IFMT) == S_IFDIR) {
    rmdir(req_path);
  } else {
    unlink(req_path);
  }

  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
  header.res.op = CSIEBOX_PROTOCOL_OP_RM;
  header.res.datalen = 0;
  header.res.client_id = conn_fd;
  header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
  send_message(conn_fd, &header, sizeof(header));
}
