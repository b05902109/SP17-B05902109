#include "csiebox_server.h"

#include "csiebox_common.h"
#include "connect.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/time.h>

static int parse_arg(csiebox_server* server, int argc, char** argv);
static void handle_request(csiebox_server* server, int conn_fd);
static int get_account_info(
  csiebox_server* server,  const char* user, csiebox_account_info* info);
static void login(
  csiebox_server* server, int conn_fd, csiebox_protocol_login* login);
static void logout(csiebox_server* server, int conn_fd);
static char* get_user_homedir(
  csiebox_server* server, csiebox_client_info* info);

#define DIR_S_FLAG (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)//permission you can use to create new file
#define REG_S_FLAG (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)//permission you can use to create new directory

//read config file, and start to listen
void csiebox_server_init(
  csiebox_server** server, int argc, char** argv) {
  csiebox_server* tmp = (csiebox_server*)malloc(sizeof(csiebox_server));
  if (!tmp) {
    fprintf(stderr, "server malloc fail\n");
    return;
  }
  memset(tmp, 0, sizeof(csiebox_server));
  if (!parse_arg(tmp, argc, argv)) {
    fprintf(stderr, "Usage: %s [config file]\n", argv[0]);
    free(tmp);
    return;
  }
  int fd = server_start();
  if (fd < 0) {
    fprintf(stderr, "server fail\n");
    free(tmp);
    return;
  }
  tmp->client = (csiebox_client_info**)
      malloc(sizeof(csiebox_client_info*) * getdtablesize());
  if (!tmp->client) {
    fprintf(stderr, "client list malloc fail\n");
    close(fd);
    free(tmp);
    return;
  }
  memset(tmp->client, 0, sizeof(csiebox_client_info*) * getdtablesize());
  tmp->listen_fd = fd;
  *server = tmp;
}

//wait client to connect and handle requests from connected socket fd
int csiebox_server_run(csiebox_server* server) {
  int conn_fd, conn_len;
  struct sockaddr_in addr;
  while (1) {
    memset(&addr, 0, sizeof(addr));
    conn_len = 0;
    // waiting client connect
    conn_fd = accept(
      server->listen_fd, (struct sockaddr*)&addr, (socklen_t*)&conn_len);
    if (conn_fd < 0) {
      if (errno == ENFILE) {
          fprintf(stderr, "out of file descriptor table\n");
          continue;
        } else if (errno == EAGAIN || errno == EINTR) {
          continue;
        } else {
          fprintf(stderr, "accept err\n");
          fprintf(stderr, "code: %s\n", strerror(errno));
          break;
        }
    }
    // handle request from connected socket fd
    handle_request(server, conn_fd);
  }
  return 1;
}

void csiebox_server_destroy(csiebox_server** server) {
  csiebox_server* tmp = *server;
  *server = 0;
  if (!tmp) {
    return;
  }
  close(tmp->listen_fd);
  int i = getdtablesize() - 1;
  for (; i >= 0; --i) {
    if (tmp->client[i]) {
      free(tmp->client[i]);
    }
  }
  free(tmp->client);
  free(tmp);
}

//read config file
static int parse_arg(csiebox_server* server, int argc, char** argv) {
  if (argc != 2) {
    return 0;
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
  int accept_config_total = 2;
  int accept_config[2] = {0, 0};
  while ((keylen = getdelim(&key, &keysize, '=', file) - 1) > 0) {
    key[keylen] = '\0';
    vallen = getline(&val, &valsize, file) - 1;
    val[vallen] = '\0';
    fprintf(stderr, "config (%d, %s)=(%d, %s)\n", keylen, key, vallen, val);
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

//It is a sample function
//you may remove it after understanding
int sampleFunction(int conn_fd, csiebox_protocol_meta* meta) {
  
  printf("In sampleFunction:\n");
  uint8_t hash[MD5_DIGEST_LENGTH];
  memset(&hash, 0, sizeof(hash));
  md5_file(".gitignore", hash);
  printf("pathlen: %d\n", meta->message.body.pathlen);
  if (memcmp(hash, meta->message.body.hash, strlen(hash)) == 0) {
    printf("hashes are equal!\n");
  }

  //use the pathlen from client to recv path 
  char buf[400];
  memset(buf, 0, sizeof(buf));
  recv_message(conn_fd, buf, meta->message.body.pathlen);
  printf("This is the path from client:%s\n", buf);

  //send OK to client
  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
  header.res.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
  header.res.datalen = 0;
  header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
  if(!send_message(conn_fd, &header, sizeof(header))){
    fprintf(stderr, "send fail\n");
    return 0;
  }

  return 1;
}
void meta_sync(char *path, struct stat *temp_stat){
  //printf("%s is %o\n", path, (temp_stat->st_mode)&0x1ff);
  chmod(path, temp_stat->st_mode);
  struct timeval tvp[2];
  tvp[0].tv_sec = temp_stat->st_atime;
  tvp[0].tv_usec = 0;
  tvp[1].tv_sec = temp_stat->st_mtime;
  tvp[1].tv_usec = 0;
  if(lutimes(path, tvp)<0)
    fprintf(stderr, "meta sync fail %s\n", path);
}

//this is where the server handle requests, you should write your code here
static void handle_request(csiebox_server* server, int conn_fd) {
  char buf[400], dir_path[400], content[4010];
  char src_dir[400], target_dir[400];
  struct stat temp_stat;
  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  while (recv_message(conn_fd, &header, sizeof(header))) {
    if (header.req.magic != CSIEBOX_PROTOCOL_MAGIC_REQ) {
      continue;
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
          memset(buf, 0, sizeof(buf));
          memset(dir_path, 0, sizeof(dir_path));
          recv_message(conn_fd, buf, meta.message.body.pathlen);
          sprintf(dir_path, "%s/%s/%s", server->arg.path, server->client[conn_fd]->account.user, buf);
          temp_stat.st_mode = meta.message.body.stat.st_mode;
          temp_stat.st_atime = meta.message.body.stat.st_atime;
          temp_stat.st_mtime = meta.message.body.stat.st_mtime;
          if(S_ISLNK(temp_stat.st_mode)){//sync symbolic link
            memset(content, 0, sizeof(content));
            csiebox_protocol_file file;
            recv_message(conn_fd, &file, sizeof(file));
            recv_message(conn_fd, content, file.message.body.datalen);
            if(symlink(content, dir_path)<0)
              fprintf(stderr, "symlink build fail : %s\n", dir_path);
            struct timeval tvp[2];
            tvp[0].tv_sec = temp_stat.st_atime;
            tvp[0].tv_usec = 0;
            tvp[1].tv_sec = temp_stat.st_mtime;
            tvp[1].tv_usec = 0;
            if(lutimes(dir_path, tvp)<0)
              fprintf(stderr, "meta sync fail %s\n", dir_path);
          }
          else if(S_ISDIR(temp_stat.st_mode)){//sync directory
            DIR* dir = opendir(dir_path);
            if(dir){
              closedir(dir);
              //fprintf(stderr, "directory exist : %s\n", dir_path);
              meta_sync(dir_path, &temp_stat);
            } 
            else if(ENOENT == errno){//dir not exist
              if(mkdir(dir_path, temp_stat.st_mode)<0)
                fprintf(stderr, "directory build fail : %s\n", dir_path);
              /*
              else
                fprintf(stderr, "directory build success : %s\n", dir_path);
              */
              meta_sync(dir_path, &temp_stat);
            }
            else{
              fprintf(stderr, "something error in build directory : %s\n", dir_path);
            }
          }
          
        }
        break;
      case CSIEBOX_PROTOCOL_OP_SYNC_FILE:
        fprintf(stderr, "sync file\n");
        csiebox_protocol_file file;
        if (complete_message_with_header(conn_fd, &header, &file)) {
          memset(content, 0, sizeof(content));
          recv_message(conn_fd, content, file.message.body.datalen);
          remove(dir_path);
          int file_fd = open(dir_path, O_WRONLY | O_CREAT | O_TRUNC);
          if(file_fd < 0)
            fprintf(stderr, "file build fail : %s\n", dir_path);
          write(file_fd, content, file.message.body.datalen);
          close(file_fd);
          meta_sync(dir_path, &temp_stat);
          //printf("%s is %o\n", dir_path, temp_stat.st_mode&0x1ff);
          //====================
          //        TODO
          //====================
        }
        break;
      case CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK:
        fprintf(stderr, "sync hardlink\n");
        csiebox_protocol_hardlink hardlink;
        if (complete_message_with_header(conn_fd, &header, &hardlink)) {
          memset(src_dir, 0, sizeof(src_dir));
          memset(target_dir, 0, sizeof(target_dir));
          memset(buf, 0, sizeof(buf));
          recv_message(conn_fd, buf, hardlink.message.body.srclen);
          sprintf(src_dir, "%s/%s/%s", server->arg.path, server->client[conn_fd]->account.user, buf);
          memset(buf, 0, sizeof(buf));
          recv_message(conn_fd, buf, hardlink.message.body.targetlen);
          sprintf(target_dir, "%s/%s/%s", server->arg.path, server->client[conn_fd]->account.user, buf);
          if(link(src_dir, target_dir)<0)
            fprintf(stderr, "hard link build fail : %s\n", target_dir);
          meta_sync(target_dir, &temp_stat);
        }
        break;
      case CSIEBOX_PROTOCOL_OP_SYNC_END:
        fprintf(stderr, "sync end\n");
        csiebox_protocol_header end;
          //====================
          //        TODO
          //====================
        break;
      case CSIEBOX_PROTOCOL_OP_RM:
        fprintf(stderr, "rm\n");
        csiebox_protocol_rm rm;
        if (complete_message_with_header(conn_fd, &header, &rm)) {
          memset(buf, 0, sizeof(buf));
          memset(dir_path, 0, sizeof(dir_path));
          recv_message(conn_fd, buf, rm.message.body.pathlen);
          sprintf(dir_path, "%s/%s/%s", server->arg.path, server->client[conn_fd]->account.user, buf);
          fprintf(stderr, "rm : %s\n", dir_path);
          remove(dir_path);
        }
        break;
      default:
        fprintf(stderr, "unknown op %x\n", header.req.op);
        break;
    }
  }
  fprintf(stderr, "end of connection\n");
  logout(server, conn_fd);
}

//open account file to get account information
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
      fprintf(stderr, "illegal form in account file, line %d\n", line);
      continue;
    }
    if (strcmp(user, u) == 0) {
      memcpy(info->user, user, strlen(user));
      char* passwd = strtok(NULL, ",");
      if (!passwd) {
        fprintf(stderr, "illegal form in account file, line %d\n", line);
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

//handle the login request from client
static void login(
  csiebox_server* server, int conn_fd, csiebox_protocol_login* login) {
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

static char* get_user_homedir(
  csiebox_server* server, csiebox_client_info* info) {
  char* ret = (char*)malloc(sizeof(char) * PATH_MAX);
  memset(ret, 0, PATH_MAX);
  sprintf(ret, "%s/%s", server->arg.path, info->account.user);
  return ret;
}

