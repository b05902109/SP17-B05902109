#ifndef _CSIEBOX_COMMON_
#define _CSIEBOX_COMMON_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <bsd/md5.h>
#include <sys/stat.h>
#include <time.h>

#define DIR_S_FLAG (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
#define REG_S_FLAG (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

/*
 * protocol
 */

#define USER_LEN_MAX 30
#define PASSWD_LEN_MAX 30

typedef enum {
  CSIEBOX_PROTOCOL_MAGIC_REQ = 0x90,
  CSIEBOX_PROTOCOL_MAGIC_RES = 0x91,
} csiebox_protocol_magic;

typedef enum {
  CSIEBOX_PROTOCOL_OP_LOGIN = 0x00,
  CSIEBOX_PROTOCOL_OP_SYNC_META = 0x01,
  CSIEBOX_PROTOCOL_OP_SYNC_FILE = 0x02,
  CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK = 0x03,
  CSIEBOX_PROTOCOL_OP_SYNC_END = 0x04,
  CSIEBOX_PROTOCOL_OP_RM = 0x05,
  CSIEBOX_PROTOCOL_OP_TIME = 0x06,
} csiebox_protocol_op;

typedef enum {
  CSIEBOX_PROTOCOL_STATUS_OK = 0x00,
  CSIEBOX_PROTOCOL_STATUS_FAIL = 0x01,
  CSIEBOX_PROTOCOL_STATUS_MORE = 0x02,
  CSIEBOX_PROTOCOL_STATUS_BUSY = 0x03,
} csiebox_protocol_status;

typedef union {
  struct {
    uint8_t magic;
    uint8_t op;
    uint8_t status;
    uint16_t client_id;
    uint32_t datalen;
  } req;
  struct {
    uint8_t magic;
    uint8_t op;
    uint8_t status;
    uint16_t client_id;
    uint32_t datalen;
  } res;
  uint8_t bytes[9];
} csiebox_protocol_header;

typedef union {
  struct {
    csiebox_protocol_header header;
    struct {
      uint8_t user[USER_LEN_MAX];
      uint8_t passwd_hash[MD5_DIGEST_LENGTH];
    } body;
  } message;
  uint8_t bytes[sizeof(csiebox_protocol_header) +
                USER_LEN_MAX +
                MD5_DIGEST_LENGTH];
} csiebox_protocol_login;

typedef union {
  struct {
    csiebox_protocol_header header;
    struct {
      uint32_t pathlen;
      struct stat stat;
      uint8_t hash[MD5_DIGEST_LENGTH];
    } body;
  } message;
  uint8_t bytes[sizeof(csiebox_protocol_header) +
                4 +
                sizeof(struct stat) +
                MD5_DIGEST_LENGTH];
} csiebox_protocol_meta;

typedef union {
  struct {
    csiebox_protocol_header header;
    struct {
      uint64_t datalen;
    } body;
  } message;
  uint8_t bytes[sizeof(csiebox_protocol_header) + 8];
} csiebox_protocol_file;

typedef union {
  struct {
    csiebox_protocol_header header;
    struct {
      uint32_t srclen;
      uint32_t targetlen;
    } body;
  } message;
  uint8_t bytes[sizeof(csiebox_protocol_header) + 8];
} csiebox_protocol_hardlink;

typedef union {
  struct {
    csiebox_protocol_header header;
    struct {
      uint32_t pathlen;
    } body;
  } message;
  uint8_t bytes[sizeof(csiebox_protocol_header) + 4];
} csiebox_protocol_rm;

typedef union {
  struct {
    csiebox_protocol_header header;
    struct {
      time_t c1;
      time_t c2;
      time_t s1;
      time_t s2;
    } body;
  } message;
  uint8_t bytes[sizeof(time_t) * 4];
} csiebox_protocol_time;

/*
 * utility
 */

// do md5 hash
void md5(const char* str, size_t len, uint8_t digest[MD5_DIGEST_LENGTH]);

// do file md5
int md5_file(const char* path, uint8_t digest[MD5_DIGEST_LENGTH]);

// recv message
int recv_message(int conn_fd, void* message, size_t len);

// copy header and recv remain part of message
int complete_message_with_header(
  int conn_fd, csiebox_protocol_header* header, void* result);

// send message
int send_message(int conn_fd, void* message, size_t len);

int get_hash_code(const char* path);
void get_dir_and_name(const char* target, char* dir, char* name);
int md5_link(const char* path, uint8_t digest[MD5_DIGEST_LENGTH]);

#ifdef __cplusplus
}
#endif

#endif
