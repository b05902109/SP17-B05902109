#include "csiebox_client.h"

#include "csiebox_common.h"
#include "connect.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <linux/inotify.h> //header for inotify
#include <string.h>

#include <dirent.h>
#include <sys/stat.h>
//#include <fcntl.h>
#include <stdbool.h>

static int parse_arg(csiebox_client* client, int argc, char** argv);
static int login(csiebox_client* client);

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))

char longestPath[301] = "\0";
int longestDepth = 0;
ino_t all_file_inode[350];
char all_file_path[350][301];
int all_file_n = 0;

int inotify_fd;
int wd_table[301], wd_table_n;
char wd_path[301][301];

//read config file, and connect to server
void csiebox_client_init(
  csiebox_client** client, int argc, char** argv) {
  csiebox_client* tmp = (csiebox_client*)malloc(sizeof(csiebox_client));
  if (!tmp) {
    fprintf(stderr, "client malloc fail\n");
    return;
  }
  memset(tmp, 0, sizeof(csiebox_client));
  if (!parse_arg(tmp, argc, argv)) {
    fprintf(stderr, "Usage: %s [config file]\n", argv[0]);
    free(tmp);
    return;
  }
  int fd = client_start(tmp->arg.name, tmp->arg.server);
  if (fd < 0) {
    fprintf(stderr, "connect fail\n");
    free(tmp);
    return;
  }
  tmp->conn_fd = fd;
  *client = tmp;
}

//show how to use csiebox_protocol_meta header
//other headers is similar usage
//please check out include/common.h
//using .gitignore for example only for convenience  
int sampleFunction(csiebox_client* client){
  csiebox_protocol_meta req;
  memset(&req, 0, sizeof(req));
  req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
  req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
  req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
  char path[] = "just/a/testing/path";
  req.message.body.pathlen = strlen(path);

  //just show how to use these function
  //Since there is no file at "just/a/testing/path"
  //I use ".gitignore" to replace with
  //In fact, it should be 
  //lstat(path, &req.message.body.stat);
  //md5_file(path, req.message.body.hash);
  lstat(".gitignore", &req.message.body.stat);
  md5_file(".gitignore", req.message.body.hash);


  //send pathlen to server so that server can know how many charachers it should receive
  //Please go to check the samplefunction in server
  if (!send_message(client->conn_fd, &req, sizeof(req))) {
    fprintf(stderr, "send fail\n");
    return 0;
  }

  //send path
  send_message(client->conn_fd, path, strlen(path));

  //receive csiebox_protocol_header from server
  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  if (recv_message(client->conn_fd, &header, sizeof(header))) {
    if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
        header.res.op == CSIEBOX_PROTOCOL_OP_SYNC_META &&
        header.res.status == CSIEBOX_PROTOCOL_STATUS_OK) {
      printf("Receive OK from server\n");
      return 1;
    } else {
      return 0;
    }
  }
  return 0;
}

void traverse_cdir(csiebox_client* client, char *dir_path, int NowDepth){
  //set inotify
  fprintf(stderr, "watch directory : %s\n", dir_path);
  wd_table[wd_table_n] = inotify_add_watch(inotify_fd, dir_path, IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MODIFY);
  strcpy(wd_path[wd_table_n], dir_path);
  wd_table_n++;
  
  int file_name_n = 0, i, j;
  char file_name[301][301], new_dir_path[500];
  memset(new_dir_path, 0, sizeof(new_dir_path));
  DIR *dir = opendir(dir_path);
  struct dirent *dp;
  while ((dp = readdir(dir)) != NULL) {
      strcpy(file_name[file_name_n], dp->d_name);
      file_name_n++;
  }
  closedir(dir);
  for(i=0;i<file_name_n;i++){
    if(strcmp(file_name[i], ".")==0 || strcmp(file_name[i], "..")==0)
      continue;
    memset(new_dir_path, 0, sizeof(new_dir_path));
    sprintf(new_dir_path, "%s/%s", dir_path, file_name[i]);
    if(NowDepth > longestDepth){
      longestDepth = NowDepth;
      strcpy(longestPath, new_dir_path);
    }
    //printf("%s\n", new_dir_path);
    struct stat stat_buff;
    lstat(new_dir_path, &stat_buff);

    if(S_ISREG(stat_buff.st_mode)){
      bool is_hardlink_and_have_create_inode = false;
      int same_inode_index;
      if(stat_buff.st_nlink >= 2){
        for(j=0 ; !is_hardlink_and_have_create_inode && j<all_file_n ; j++){
          if(all_file_inode[j]==stat_buff.st_ino)//it is a hard link now
            is_hardlink_and_have_create_inode = true;
            same_inode_index = j;
        }
      }
      csiebox_protocol_meta req_path;
      memset(&req_path, 0, sizeof(req_path));
      req_path.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
      req_path.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
      req_path.message.header.req.client_id = client->client_id;
      req_path.message.header.req.datalen = sizeof(req_path) - sizeof(req_path.message.header);
      req_path.message.body.pathlen = strlen(new_dir_path)-(strlen(client->arg.path)+1);
      lstat(new_dir_path, &(req_path.message.body.stat));
      send_message(client->conn_fd, &(req_path), sizeof(req_path));
      send_message(client->conn_fd, new_dir_path+strlen(client->arg.path)+1, strlen(new_dir_path)-(strlen(client->arg.path)+1));

      if(is_hardlink_and_have_create_inode){
        csiebox_protocol_hardlink req;
        memset(&req, 0, sizeof(req));
        req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
        req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK;
        req.message.header.req.client_id = client->client_id;
        req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
        req.message.body.srclen = strlen(all_file_path[same_inode_index]);
        req.message.body.targetlen = strlen(new_dir_path)-(strlen(client->arg.path)+1);
        send_message(client->conn_fd, &(req), sizeof(req));
        send_message(client->conn_fd, all_file_path[same_inode_index], strlen(all_file_path[same_inode_index]));
        send_message(client->conn_fd, new_dir_path+strlen(client->arg.path)+1, strlen(new_dir_path)-(strlen(client->arg.path)+1));
        //printf("%s\n%s\n", all_file_path[same_inode_index], new_dir_path+strlen(client->arg.path)+1);
        //recv res...
      }
      else{ //file' s nnode == 1
        FILE *fptr = fopen(new_dir_path, "r");
        char content[4010];
        memset(content, 0, sizeof(content));
        int content_index = 0;
        fseek(fptr, 0, SEEK_SET);
        while ((content[content_index] = fgetc(fptr)) != EOF)
          content_index++;
        fclose(fptr);
        csiebox_protocol_file req_content;
        memset(&req_content, 0, sizeof(req_content));
        req_content.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
        req_content.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_FILE;
        req_content.message.header.req.client_id = client->client_id;
        req_content.message.header.req.datalen = sizeof(req_content) - sizeof(req_content.message.header);
        req_content.message.body.datalen = content_index;
        send_message(client->conn_fd, &(req_content), sizeof(req_content));
        send_message(client->conn_fd, content, content_index);
        //recv res ...
        if(req_path.message.body.stat.st_nlink > 1){
          strcpy(all_file_path[all_file_n], new_dir_path+strlen(client->arg.path)+1);
          all_file_inode[all_file_n] = req_path.message.body.stat.st_ino;
          all_file_n++;
        }
      }
      
      //md5_file(new_dir_path, req.message.body.hash);
    }
    else if(S_ISLNK(stat_buff.st_mode)){//symbolic link
      csiebox_protocol_meta req_path;
      memset(&req_path, 0, sizeof(req_path));
      req_path.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
      req_path.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
      req_path.message.header.req.client_id = client->client_id;
      req_path.message.header.req.datalen = sizeof(req_path) - sizeof(req_path.message.header);
      req_path.message.body.pathlen = strlen(new_dir_path)-(strlen(client->arg.path)+1);
      lstat(new_dir_path, &(req_path.message.body.stat));
      send_message(client->conn_fd, &(req_path), sizeof(req_path));
      send_message(client->conn_fd, new_dir_path+strlen(client->arg.path)+1, strlen(new_dir_path)-(strlen(client->arg.path)+1));
      //send file content
      char content[400];
      int content_len = readlink(new_dir_path, content, 400);
      csiebox_protocol_file req_content;
      memset(&req_content, 0, sizeof(req_content));
      req_content.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
      req_content.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_FILE;
      req_content.message.header.req.client_id = client->client_id;
      req_content.message.header.req.datalen = sizeof(req_content) - sizeof(req_content.message.header);
      req_content.message.body.datalen = content_len;
      send_message(client->conn_fd, &(req_content), sizeof(req_content));
      send_message(client->conn_fd, content, content_len);
    }
    else if(S_ISDIR(stat_buff.st_mode)){//directory
      csiebox_protocol_meta req;
      memset(&req, 0, sizeof(req));
      req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
      req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
      req.message.header.req.client_id = client->client_id;
      req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
      req.message.body.pathlen = strlen(new_dir_path)-(strlen(client->arg.path)+1);
      lstat(new_dir_path, &(req.message.body.stat));
      send_message(client->conn_fd, &(req), sizeof(req));
      send_message(client->conn_fd, new_dir_path+strlen(client->arg.path)+1, strlen(new_dir_path)-(strlen(client->arg.path)+1));
      //printf("sync meta dir  path %s\n", new_dir_path+strlen(client->arg.path)+1);

      //recv res...

      traverse_cdir(client, new_dir_path, NowDepth+1);
    }
  }
  if(strcmp(dir_path, client->arg.path)!=0){
    csiebox_protocol_meta req;
    memset(&req, 0, sizeof(req));
    req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
    req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
    req.message.header.req.client_id = client->client_id;
    req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
    req.message.body.pathlen = strlen(dir_path)-(strlen(client->arg.path)+1);
    lstat(dir_path, &(req.message.body.stat));
    send_message(client->conn_fd, &(req), sizeof(req));
    send_message(client->conn_fd, dir_path+strlen(client->arg.path)+1, strlen(dir_path)-(strlen(client->arg.path)+1));
  }
}

//this is where client sends request, you sould write your code here
int csiebox_client_run(csiebox_client* client) {
  if (!login(client)) {
    fprintf(stderr, "login fail\n");
    return 0;
  }
  fprintf(stderr, "login success\n");
  

  //This is a sample function showing how to send data using defined header in common.h
  //You can remove it after you understand
  //sampleFunction(client);
  inotify_fd = inotify_init();
  if(inotify_fd < 0)
    fprintf(stderr, "inotify initial error\n");
  wd_table_n = 0;
  traverse_cdir(client, client->arg.path, 0);

  char new_dir_path[400], buf[400];
  memset(new_dir_path, 0, sizeof(new_dir_path));
  sprintf(new_dir_path, "%s/%s", client->arg.path, "longestPath.txt");
  FILE *fptr = fopen(new_dir_path, "w");
  fwrite(longestPath+strlen(client->arg.path)+1, strlen(longestPath)-(strlen(client->arg.path)+1), 1, fptr);
  fclose(fptr);
  chmod(new_dir_path, 0777);

  csiebox_protocol_meta req_path;
  memset(&req_path, 0, sizeof(req_path));
  req_path.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
  req_path.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
  req_path.message.header.req.client_id = client->client_id;
  req_path.message.header.req.datalen = sizeof(req_path) - sizeof(req_path.message.header);
  req_path.message.body.pathlen = strlen(new_dir_path)-(strlen(client->arg.path)+1);
  lstat(new_dir_path, &(req_path.message.body.stat));
  send_message(client->conn_fd, &(req_path), sizeof(req_path));
  send_message(client->conn_fd, new_dir_path+strlen(client->arg.path)+1, strlen(new_dir_path)-(strlen(client->arg.path)+1));
  //send file content
  csiebox_protocol_file req_content;
  memset(&req_content, 0, sizeof(req_content));
  req_content.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
  req_content.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_FILE;
  req_content.message.header.req.client_id = client->client_id;
  req_content.message.header.req.datalen = sizeof(req_content) - sizeof(req_content.message.header);
  req_content.message.body.datalen = strlen(longestPath)-(strlen(client->arg.path)+1);
  send_message(client->conn_fd, &(req_content), sizeof(req_content));
  send_message(client->conn_fd, longestPath+strlen(client->arg.path)+1, strlen(longestPath)-(strlen(client->arg.path)+1));
  
  memset(new_dir_path, 0, sizeof(new_dir_path));
  sprintf(new_dir_path, "%s/.", client->arg.path);
  csiebox_protocol_meta req;
  memset(&req, 0, sizeof(req));
  req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
  req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
  req.message.header.req.client_id = client->client_id;
  req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
  req.message.body.pathlen = 1;
  lstat(new_dir_path, &(req.message.body.stat));
  send_message(client->conn_fd, &(req), sizeof(req));
  send_message(client->conn_fd, new_dir_path+strlen(client->arg.path)+1, strlen(new_dir_path)-(strlen(client->arg.path)+1));

  //====================
  //        TODO
  //====================

  //watch the whole cdir
  
  int i, index, index_goal, length, flag;
  char buffer[EVENT_BUF_LEN];
  memset(buffer, 0, EVENT_BUF_LEN);
  while ((length = read(inotify_fd, buffer, EVENT_BUF_LEN)) > 0) {
    i = 0;
    while (i < length) {
      struct inotify_event* event = (struct inotify_event*)&buffer[i];
      //fprintf(stderr, "event name : %s\n", event->name);
      if(strcmp(event->name, "longestPath.txt")==0){
        i += EVENT_SIZE + event->len;
        continue;
      }
      flag = 1;
      for(index = 0; flag && index < wd_table_n;index++){
        if(event->wd == wd_table[index]){
          index_goal = index;
          flag = 0;
        }
      }
      if(flag)
        fprintf(stderr, "not find wd in wd_table\n");
      memset(new_dir_path, 0, sizeof(new_dir_path));
      memset(buf, 0, sizeof(buf));
      sprintf(new_dir_path, "%s/%s", wd_path[index_goal], event->name);
      //fprintf(stderr, "%d\n", !(event->mask & IN_DELETE));
      if(!(event->mask & IN_DELETE)){
        csiebox_protocol_meta meta;
        memset(&req_path, 0, sizeof(meta));
        meta.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
        meta.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
        meta.message.header.req.client_id = client->client_id;
        meta.message.header.req.datalen = sizeof(meta) - sizeof(meta.message.header);
        meta.message.body.pathlen = strlen(new_dir_path)-(strlen(client->arg.path)+1);
        lstat(new_dir_path, &(meta.message.body.stat));
        send_message(client->conn_fd, &(meta), sizeof(meta));
        send_message(client->conn_fd, new_dir_path+strlen(client->arg.path)+1, strlen(new_dir_path)-(strlen(client->arg.path)+1));
        fprintf(stderr, "send meta : %s\n", new_dir_path);
      }

      if( event->mask & IN_ISDIR ){ //directory
        if(event->mask & IN_DELETE) {
          fprintf(stderr,"delete dir : %s\n",new_dir_path);
          csiebox_protocol_rm rm;
          memset(&rm, 0, sizeof(rm));
          rm.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
          rm.message.header.req.op = CSIEBOX_PROTOCOL_OP_RM;
          rm.message.header.req.client_id = client->client_id;
          rm.message.header.req.datalen = sizeof(rm) - sizeof(rm.message.header);
          rm.message.body.pathlen = strlen(new_dir_path)-(strlen(client->arg.path)+1);
          send_message(client->conn_fd, &(rm), sizeof(rm));
          send_message(client->conn_fd, new_dir_path+strlen(client->arg.path)+1, strlen(new_dir_path)-(strlen(client->arg.path)+1));
        }
        else if(event->mask & IN_CREATE ) {
          fprintf(stderr,"create dir %s\n",new_dir_path);
          wd_table[wd_table_n] = inotify_add_watch(inotify_fd, new_dir_path, IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MODIFY);
          strcpy(wd_path[wd_table_n], new_dir_path);
          wd_table_n++;
        }
        else{
          fprintf(stderr, "directory Attribute modify : %s\n", new_dir_path);
        }
      }
      else{ //file
        if(event->mask & IN_DELETE) {
          fprintf(stderr,"delete file : %s\n",new_dir_path);
          csiebox_protocol_rm rm;
          memset(&rm, 0, sizeof(rm));
          rm.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
          rm.message.header.req.op = CSIEBOX_PROTOCOL_OP_RM;
          rm.message.header.req.client_id = client->client_id;
          rm.message.header.req.datalen = sizeof(rm) - sizeof(rm.message.header);
          rm.message.body.pathlen = strlen(new_dir_path)-(strlen(client->arg.path)+1);
          send_message(client->conn_fd, &(rm), sizeof(rm));
          send_message(client->conn_fd, new_dir_path+strlen(client->arg.path)+1, strlen(new_dir_path)-(strlen(client->arg.path)+1));
        }
        else{
          fprintf(stderr, "new file / modify file : %s\n", new_dir_path);
          FILE *fptr = fopen(new_dir_path, "r");
          char content[4010];
          memset(content, 0, sizeof(content));
          int content_index = 0;
          fseek(fptr, 0, SEEK_SET);
          while ((content[content_index] = fgetc(fptr)) != EOF)
            content_index++;
          fclose(fptr);
          csiebox_protocol_file req_content;
          memset(&req_content, 0, sizeof(req_content));
          req_content.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
          req_content.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_FILE;
          req_content.message.header.req.client_id = client->client_id;
          req_content.message.header.req.datalen = sizeof(req_content) - sizeof(req_content.message.header);
          req_content.message.body.datalen = content_index;
          send_message(client->conn_fd, &(req_content), sizeof(req_content));
          send_message(client->conn_fd, content, content_index);
        }
      }
      i += EVENT_SIZE + event->len;
    }
    fprintf(stderr, "---> One buffer end <---\n");
    memset(buffer, 0, EVENT_BUF_LEN);
  }
  
  
  return 1;
}

void csiebox_client_destroy(csiebox_client** client) {
  csiebox_client* tmp = *client;
  *client = 0;
  if (!tmp) {
    return;
  }
  close(tmp->conn_fd);
  free(tmp);
}

//read config file
static int parse_arg(csiebox_client* client, int argc, char** argv) {
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
  int accept_config_total = 5;
  int accept_config[5] = {0, 0, 0, 0, 0};
  while ((keylen = getdelim(&key, &keysize, '=', file) - 1) > 0) {
    key[keylen] = '\0';
    vallen = getline(&val, &valsize, file) - 1;
    val[vallen] = '\0';
    fprintf(stderr, "config (%d, %s)=(%d, %s)\n", keylen, key, vallen, val);
    if (strcmp("name", key) == 0) {
      if (vallen <= sizeof(client->arg.name)) {
        strncpy(client->arg.name, val, vallen);
        accept_config[0] = 1;
      }
    } else if (strcmp("server", key) == 0) {
      if (vallen <= sizeof(client->arg.server)) {
        strncpy(client->arg.server, val, vallen);
        accept_config[1] = 1;
      }
    } else if (strcmp("user", key) == 0) {
      if (vallen <= sizeof(client->arg.user)) {
        strncpy(client->arg.user, val, vallen);
        accept_config[2] = 1;
      }
    } else if (strcmp("passwd", key) == 0) {
      if (vallen <= sizeof(client->arg.passwd)) {
        strncpy(client->arg.passwd, val, vallen);
        accept_config[3] = 1;
      }
    } else if (strcmp("path", key) == 0) {
      if (vallen <= sizeof(client->arg.path)) {
        strncpy(client->arg.path, val, vallen);
        accept_config[4] = 1;
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

static int login(csiebox_client* client) {
  csiebox_protocol_login req;
  memset(&req, 0, sizeof(req));
  req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
  req.message.header.req.op = CSIEBOX_PROTOCOL_OP_LOGIN;
  req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
  memcpy(req.message.body.user, client->arg.user, strlen(client->arg.user));
  md5(client->arg.passwd,
      strlen(client->arg.passwd),
      req.message.body.passwd_hash);
  if (!send_message(client->conn_fd, &req, sizeof(req))) {
    fprintf(stderr, "send fail\n");
    return 0;
  }
  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  if (recv_message(client->conn_fd, &header, sizeof(header))) {
    if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
        header.res.op == CSIEBOX_PROTOCOL_OP_LOGIN &&
        header.res.status == CSIEBOX_PROTOCOL_STATUS_OK) {
      client->client_id = header.res.client_id;
      return 1;
    } else {
      return 0;
    }
  }
  return 0;
}
