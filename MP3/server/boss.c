#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <openssl/md5.h>

#include "boss.h"

#define maindebug 1
#define Treasure_Limit 1000000

typedef enum {
    everyone_start = 0x90,
    client_find_treasure = 0x91,
    status = 0x92,
    quit = 0x93,
    client_not_found = 0x94,
} protocol_order;

typedef struct MyProtocol{
    MD5_CTX md5_piece;
    char passing_string[100];        // \x00\x00\x00\x00\x00\x00\x00\x00
    char name[100];
    int max_treasure_num;
    int order;                      // Myprotocol
    int valid_index;                // 1 ~ 8
    int passing_string_n;
    unsigned long long start, end;  // valid_index = 2, \x00\x00 ~ \xff\xff
} myProtocol;

typedef struct Dump_task{
    char path[5000];
    int dump_len;
    struct Dump_task *next;
}dump_task;


MD5_CTX best_treasure, tmp_CTX;
char tmp_char, out[33], tmp_str[5000];
int best_treasure_num, best_treasure_len;
void md5_make(char *out, MD5_CTX *input_string){
    unsigned char digest[17];
    int i;
    memset(digest, 0, sizeof(digest));
    MD5_Final(digest, input_string);
    for (i = 0; i < 16; i++) 
        sprintf(&(out[i*2]), "%02x", (unsigned int)digest[i]);
    out[32] = '\0';
    return;
}

int load_config_file(struct server_config *config, char *path){
    /* TODO finish your own config file parser */
    //printf("start\n");
    char buff1[10], buff2[5000], buff3[5000];
    int buff_index;
    FILE *fptr = fopen(path, "r");
    //if(!fptr)   printf("open file fail\n");
    
    //mine file path
    fseek(fptr, 0, SEEK_SET);
    memset(buff1, 0, sizeof(buff1));
    memset(buff2, 0, sizeof(buff2));
    fscanf(fptr, "%s %s", buff1, buff2);
    config->mine_file = (char*)malloc( sizeof(char)*(strlen(buff2)+1) );
    strcpy(config->mine_file, buff2);
    
    config->pipes = NULL;
    config->num_miners = 0;
    memset(buff1, 0, sizeof(buff1));
    memset(buff2, 0, sizeof(buff2));
    memset(buff3, 0, sizeof(buff3));
    while(fscanf(fptr, "%s %s %s", buff1, buff2, buff3)!=EOF){
        config->num_miners ++;
        config->pipes = realloc(config->pipes, sizeof(struct pipe_pair)*(config->num_miners));
        config->pipes[config->num_miners - 1].output_pipe = (char*)malloc(sizeof(char)*strlen(buff2));
        config->pipes[config->num_miners - 1].input_pipe = (char*)malloc(sizeof(char)*strlen(buff3));
        strcpy(config->pipes[config->num_miners - 1].input_pipe, buff2);    //ada_in in input
        strcpy(config->pipes[config->num_miners - 1].output_pipe, buff3);
        memset(buff1, 0, sizeof(buff1));
        memset(buff2, 0, sizeof(buff2));
        memset(buff3, 0, sizeof(buff3));
    }
    fclose(fptr);
    return 0;
}

int main(int argc, char **argv)
{
    /* sanity check on arguments */
    if (argc != 2){
        fprintf(stderr, "Usage: %s CONFIG_FILE\n", argv[0]);
        exit(1);
    }

    /* load config file */
    struct server_config config;
    load_config_file(&config, argv[1]);

//#if maindebug == 1
    /* open the named pipes */
    dump_task *task = NULL;
    int i, j, k, check_best, treasure_fd, target_fd, fd;
    struct fd_pair client_fds[config.num_miners];
    struct timeval timeout;
    fd_set readset;
    fd_set working_readset;
    char path[5000], get_stdin[100];
    int ret, resting_miner = 0, maxfd = 0;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    for (i = 0; i < config.num_miners; i += 1){
        struct fd_pair *fd_ptr = &client_fds[i];
        struct pipe_pair *pipe_ptr = &config.pipes[i];
        fd_ptr->input_fd = open(pipe_ptr->input_pipe, O_WRONLY);   //ada_in ada read
        fd_ptr->output_fd = open(pipe_ptr->output_pipe, O_RDONLY);
    }

    //send first job
    unsigned long long range, section, boss_start, boss_end;
    int boss_valid_index = 1;
    range = 256;
    section = range/config.num_miners;
    boss_start = 0;
    boss_end = boss_start + section - 1;

    //set local treasure
    MD5_Init(&best_treasure);
    fd = open(config.mine_file, O_RDONLY);
    i = 0;
    best_treasure_num = 0;
    best_treasure_len = 0;
    FD_ZERO(&readset);
    FD_SET(STDIN_FILENO, &readset);
    while((j = read(fd, &tmp_str, 4096))!=0){
        memcpy(&working_readset, &readset, sizeof(readset));
        ret = select(STDIN_FILENO+1, &working_readset, NULL, NULL, &timeout);
        if(ret>0){
            memset(get_stdin, 0, sizeof(get_stdin));
            scanf("%s", get_stdin);
            if(strcmp(get_stdin, "status")==0){\
                printf("best 0-treasure in 0 bytes\n");
                fprintf(stderr, "read %d\n", i);
            }
            else if(strcmp(get_stdin, "dump")==0){
                target_fd = open(path, O_WRONLY | O_CREAT, 0666);
                close(target_fd);
            }
            else if(strcmp(get_stdin, "quit")==0){
                myProtocol message;
                message.order = quit;
                for(i = 0; i < config.num_miners; i++)
                    write(client_fds[i].input_fd, &message, sizeof(message));
                return 0;
            }
        }
        i += j;
        MD5_Update(&best_treasure, tmp_str, j);
        /*
        for(k = 0;k < j;k++){
            MD5_Update(&best_treasure, &(tmp_str[k]), 1);
            memcpy(&tmp_CTX, &best_treasure, sizeof(best_treasure));
            md5_make(out, &tmp_CTX);
            check_best = 0;
            while(out[check_best]=='0'){
                check_best++;
            }
            if(check_best > 0){
                //fprintf(stderr, "check_best = %d\n", check_best);
                if(best_treasure_num + 1 == check_best)
                    best_treasure_num = check_best;
            }
        }
        */
    }
    best_treasure_len = i;

    fprintf(stderr, "---best_treasure_num = %d, best_treasure_len = %d---\n", best_treasure_num, best_treasure_len);

    myProtocol message;
    memset(&message, 0, sizeof(message));
    message.order = everyone_start;
    message.max_treasure_num = best_treasure_num;
    memcpy(&message.md5_piece, &best_treasure, sizeof(best_treasure));
    message.valid_index = boss_valid_index;
    for(i = 0; i < config.num_miners; i++){
        message.start = boss_start;
        if(i == config.num_miners-1)
            boss_end = range - 1;
        message.end = boss_end;
        write(client_fds[i].input_fd, &message, sizeof(message));
        boss_start = boss_end + 1;
        boss_end = boss_start + section - 1;
    }

    //set steady select set
    FD_ZERO(&readset);
    FD_SET(STDIN_FILENO, &readset);
    if(STDIN_FILENO > maxfd)
        maxfd = STDIN_FILENO;
    for(i=0;i<config.num_miners;i++){
        FD_SET(client_fds[i].output_fd, &readset);
        if(client_fds[i].output_fd > maxfd)
            maxfd = client_fds[i].output_fd;
    }
    while (1){
        //fprintf(stderr, "listen...\n");
        //sleep(1);
        memcpy(&working_readset, &readset, sizeof(readset));
        ret = select(maxfd+1, &working_readset, NULL, NULL, &timeout);
        if(ret == -1){
            fprintf(stderr, "select error\n");
            break;
        }
        else if(ret > 0){
            //listen pipe
            //fprintf(stderr, "listen a message\n");
            if( FD_ISSET(STDIN_FILENO, &working_readset) ){
                memset(get_stdin, 0, sizeof(get_stdin));
                scanf("%s", get_stdin);
                if(strcmp(get_stdin, "status")==0){
                    //fprintf(stderr, "stdin command is %s\n", get_stdin);
                    memcpy(&tmp_CTX, &best_treasure, sizeof(best_treasure));
                    md5_make(out, &tmp_CTX);
                    if(best_treasure_num == 0)
                        printf("best 0-treasure in 0 bytes\n");
                    else
                        printf("best %d-treasure %s in %d bytes\n", best_treasure_num, out, best_treasure_len);
                    myProtocol message;
                    message.order = status;
                    for(i = 0; i < config.num_miners; i++)
                        write(client_fds[i].input_fd, &message, sizeof(message));
                }
                
                else if(strcmp(get_stdin, "dump")==0){
                    dump_task *tmp_task = (dump_task*)malloc(sizeof(dump_task));
                    tmp_task->next = task;
                    tmp_task->dump_len = best_treasure_len;
                    scanf("%s", tmp_task->path);
                    task = tmp_task;
                    fprintf(stderr, "dump to %s in %d bytes\n", task->path, task->dump_len);
                }
                
                else if(strcmp(get_stdin, "quit")==0){
                    myProtocol message;
                    message.order = quit;
                    for(i = 0; i < config.num_miners; i++)
                        write(client_fds[i].input_fd, &message, sizeof(message));
                    break;
                }
                else{
                    //fprintf(stderr, "stdin have an unknown input\n");
                }
            }
            else{
                int is_found = 0;
                for(i = 0; i < config.num_miners; i++){
                    if( FD_ISSET(client_fds[i].output_fd, &working_readset) ){
                        is_found = 1;
                        myProtocol get_message;
                        read(client_fds[i].output_fd, &get_message, sizeof(get_message));
                        if(get_message.order == client_find_treasure && get_message.max_treasure_num > best_treasure_num){
                            //someone get a treasure
                            best_treasure_num = get_message.max_treasure_num;
                            memcpy(&best_treasure, &get_message.md5_piece, sizeof(get_message.md5_piece));
                            best_treasure_len += get_message.passing_string_n;
                            //fprintf(stderr, "len is %d\n", best_treasure_len);
                            memcpy(&tmp_CTX, &best_treasure, sizeof(best_treasure));
                            md5_make(out, &tmp_CTX);
                            printf("A %d-treasure discovered! %s\n", best_treasure_num, out);
                            boss_valid_index = 1;
                            range = 256;
                            section = range / config.num_miners;
                            boss_start = 0;
                            boss_end = boss_start + section - 1;

                            myProtocol message;
                            message.order = everyone_start;
                            message.valid_index = boss_valid_index;
                            message.max_treasure_num = best_treasure_num;
                            memcpy(&message.md5_piece, &best_treasure, sizeof(best_treasure));
                            strcpy(message.name, get_message.name);
                            for(j = 0; j < config.num_miners; j++){
                                message.start = boss_start;
                                if(j == config.num_miners-1)
                                    boss_end = range - 1;
                                message.end = boss_end;
                                write(client_fds[j].input_fd, &message, sizeof(message));
                                boss_start = boss_end + 1;
                                boss_end = boss_start + section - 1;
                            }
                            
                            int tmp_fd = open(config.mine_file, O_WRONLY|O_APPEND);
                            if( write(tmp_fd, get_message.passing_string, get_message.passing_string_n) < 0)
                                fprintf(stderr, "write mine.bin fail\n");
                            close(tmp_fd);
                            fprintf(stderr, "write best %d treasure, %d bytes\n", best_treasure_num, best_treasure_len);
                        }
                        else if(get_message.order == client_not_found){
                            //one miner get nothing and resting for next order
                            resting_miner ++;
                            /*
                            if(best_treasure_num < get_message.max_treasure_num)
                                best_treasure_num = get_message.max_treasure_num;
                            */
                            if(resting_miner == config.num_miners){
                                //this bits no treasure, allocate new works
                                resting_miner = 0;
                                boss_valid_index ++;
                                range <<= 8;
                                section = range / config.num_miners;
                                boss_start = 0;
                                boss_end = boss_start + section - 1;

                                myProtocol message;
                                message.order = everyone_start;
                                message.valid_index = boss_valid_index;
                                message.max_treasure_num = best_treasure_num;
                                for(j = 0; j < config.num_miners; j++){
                                    message.start = boss_start;
                                    if(j == config.num_miners-1)
                                        boss_end = range - 1;
                                    message.end = boss_end;
                                    write(client_fds[j].input_fd, &message, sizeof(message));
                                    boss_start = boss_end + 1;
                                    boss_end = boss_start + section - 1;
                                }
                            }
                        }
                        else{
                            fprintf(stderr, "unexpected order happened??\n");
                        }
                        break;
                    }
                }
                if(!is_found){
                    fprintf(stderr, "select something, not stdin, but i dont know ...\n");
                    break;
                }
            }
        }
        else{
            continue;
        }
    }
    int dump_once_len;
    treasure_fd = open(config.mine_file, O_RDONLY);
    for(dump_task *now = task ; now != NULL ; now = now->next){
        fprintf(stderr, "%s in %d bytes\n", now->path, now->dump_len);
        lseek(treasure_fd, 0, SEEK_SET);
        target_fd = open(now->path, O_WRONLY | O_TRUNC | O_CREAT, 0666);
        while(now->dump_len > 4096){
            now->dump_len -= 4096;
            read(treasure_fd, &tmp_str, 4096);
            write(target_fd, &tmp_str, 4096);
        }
        read(treasure_fd, &tmp_str, now->dump_len);
        write(target_fd, &tmp_str, now->dump_len);
        close(target_fd);
    }

    for(i = 0; i < config.num_miners; i++){
        close(client_fds[i].input_fd);
        close(client_fds[i].output_fd);
    }

//#endif
    return 0;
}
