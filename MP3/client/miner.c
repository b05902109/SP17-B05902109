#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#include <openssl/md5.h>
#include <string.h>
#include <sys/select.h>

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


MD5_CTX best_treasure, trying_treasure, tmp_CTX;
char trying_string[ Treasure_Limit ], out[33];
int miner_valid_index, best_treasure_num;
unsigned long long miner_start, miner_end, miner_trying_now;
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
int main(int argc, char **argv)
{
    /* parse arguments */
    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s CONFIG_FILE\n", argv[0]);
        exit(1);
    }

    char *miner_name = argv[1];
    char *input_pipe = argv[2];
    char *output_pipe = argv[3];
    /* create named pipes */
    int ret;
    ret = mkfifo(input_pipe, 0644); //ada_in
    ret = mkfifo(output_pipe, 0644);
    /* open pipes */
    int input_fd = open(input_pipe, O_RDONLY);  //ada_in
    int output_fd = open(output_pipe, O_WRONLY);
    
    //set local treasure
    MD5_Init(&best_treasure);
    best_treasure_num = -1;
    //set steady select set
    int is_quitting = 1, i, is_finding = 0;
    fd_set readset, working_readset;
    struct timeval timeout;
    FD_ZERO(&readset);
    FD_SET(input_fd, &readset);
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    while(1){
        //fprintf(stderr, "listen...\n");
        //sleep(1);
        memcpy(&working_readset, &readset, sizeof(readset));
        ret = select(input_fd+1, &working_readset, NULL, NULL, &timeout);
        if(ret == -1){
            printf("select error\n");
            break;
        }
        else if(ret == 0 && is_finding && !is_quitting){
            //keep mining
            //fprintf(stderr, "=======\nkeep mining\n");
            memcpy(&trying_treasure, &best_treasure, sizeof(best_treasure));
            for(i = miner_valid_index - 1; i >= 0 ; i --)
                trying_string[(miner_valid_index-i-1)] = (miner_trying_now >> (8*i)) & 255;
            char out[33];
            MD5_Update(&trying_treasure, trying_string, miner_valid_index);
            memcpy(&tmp_CTX, &trying_treasure, sizeof(trying_treasure));
            md5_make(out, &tmp_CTX);
            //fprintf(stderr, "trying_treasure %s\n", trying_treasure);
            //fprintf(stderr, "md5 %s, it is %d\n", out, miner_trying_now);
            int no_treasure = 1;
            if(out[0] == '0'){
                //maybe a treasure?
                int treasure_num = 0;
                while(out[treasure_num]=='0')
                    treasure_num++;
                if(treasure_num == best_treasure_num+1){
                    //a new n-treasure found
                    //fprintf(stderr, "it is %d\n", miner_trying_now);
                    no_treasure = 0;
                    myProtocol message;
                    memset(&message, 0, sizeof(message));
                    message.order = client_find_treasure;
                    for(i = miner_valid_index-1; i >= 0 ; i--)
                        message.passing_string[miner_valid_index - i - 1] = (miner_trying_now >> (8*i)) & 255;
                    message.passing_string_n = miner_valid_index;
                    message.max_treasure_num = treasure_num;
                    strcpy(message.name, miner_name);
                    memcpy(&message.md5_piece, &trying_treasure, sizeof(trying_treasure));
                    write(output_fd, &message, sizeof(message));
                    is_finding = 0;
                    //sleep(3);
                }
            }
            if(no_treasure){
                //fprintf(stderr, "no treasure, miner_trying_now++\n");
                miner_trying_now ++;
                if(miner_trying_now > miner_end){
                    //resting
                    is_finding = 0;
                    myProtocol message;
                    message.order = client_not_found;
                    message.max_treasure_num = best_treasure_num;
                    write(output_fd, &message, sizeof(message));
                }
            }
        }
        else if(ret > 0){
            //listen pipe
            //fprintf(stderr, "listen a message\n");
            myProtocol get_message;
            memset(&get_message, 0, sizeof(get_message));
            if( read(input_fd, &get_message, sizeof(get_message)) < 0 )
                fprintf(stderr, "listen error\n");
            if(get_message.order == everyone_start){
                miner_start = get_message.start;
                miner_end = get_message.end;
                miner_valid_index = get_message.valid_index;
                miner_trying_now = get_message.start;
                is_finding = 1;
                //fprintf(stderr, "start %d, end %d, valid_index %d\n", miner_start, miner_end, miner_valid_index);
                //fprintf(stderr, "best_treasure_num : %d, get_message: %d\n", best_treasure_num, get_message.max_treasure_num);
                if( best_treasure_num < get_message.max_treasure_num){
                    best_treasure_num = get_message.max_treasure_num;
                    memcpy(&best_treasure, &get_message.md5_piece, sizeof(get_message.md5_piece));
                    if(is_quitting){
                        is_quitting = 0;
                        printf("BOSS is mindful.\n");
                    }
                    else{
                        memcpy(&tmp_CTX, &best_treasure, sizeof(best_treasure));
                        md5_make(out, &tmp_CTX);
                        if(strcmp(get_message.name, miner_name)==0)
                            printf("I win a %d-treasure! %s\n", get_message.max_treasure_num, out);
                        else
                            printf("%s wins a %d-treasure! %s\n", get_message.name, get_message.max_treasure_num, out);
                    }
                }
            }
            else if(get_message.order == status){
                memcpy(&tmp_CTX, &trying_treasure, sizeof(trying_treasure));
                md5_make(out, &tmp_CTX);
                printf("I'm working on %s\n", out);
            }
            else if(get_message.order == quit){
                is_quitting = 1;
                is_finding = 0;
                best_treasure_num = -1;
                printf("BOSS is at rest.\n");
                //break;  //move!!!
            }
        }
        else{
            //fprintf(stderr, "resting\n");
            continue;
        }
    }
    return 0;
}
