#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "cJSON.c"
#include <pthread.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/wait.h>
typedef struct User {
	char name[33];
	unsigned int age;
	char gender[7];
	char introduction[1025];
} User;

typedef struct MyProtocol{
    int return_value, person[2];
    User person_data[2];
} myProtocol;

User *user_table[1030];
int sockfd_table[1030], waiting_fd[1010], match_table[1030], waiting_fd_num = 0;
int worker_input_pipe_fd[5], worker_output_pipe_fd[5];
int maxfd, maxfd_forMatch;
char filename[50], program[5000], system_cmd[100], *filter_table[1030];
fd_set readset, working_readset, readset_forMatch, working_readset_forMatch;
cJSON *recv_json, *json_cmd, *json_name, *json_age, *json_gender, *json_introduction, *json_filter_function;
pid_t pid[5];

void worker(int worker_number){
	int retval, input_fd, output_fd, return1, return2;
	char buffer[30], called_filename[50], dlopen_name[50];
	myProtocol recv_protocol, send_protocol;
	void *handle;

	sprintf(buffer, "input_pipe_%d", worker_number);
    input_fd = open(buffer, O_RDONLY);
    if(input_fd<0) fprintf(stderr, "worker %d open input pipe error\n", worker_number);
	
	sprintf(buffer, "output_pipe_%d", worker_number);
    output_fd = open(buffer, O_WRONLY);
    if(output_fd<0) fprintf(stderr, "worker %d open output pipe error\n", worker_number);
    
    //fprintf(stderr, "worker_number %d is ready.\n", worker_number);
    fd_set readset, working_readset;
    struct timeval timeout;
    FD_ZERO(&readset);
    FD_SET(input_fd, &readset);
    while(1){
    	memcpy(&working_readset, &readset, sizeof(readset));
    	retval = select(input_fd+1, &working_readset, NULL, NULL, NULL);
    	if(retval>0){
    		//fprintf(stderr, "- worker_number %d select something.\n", worker_number);
    		retval = read(input_fd, &recv_protocol, sizeof(recv_protocol));
    		if(retval >= 0){
    			//fprintf(stderr, "- worker %d get job, %d and %d\n", worker_number, recv_protocol.person[0], recv_protocol.person[1]);
    			
    			send_protocol.person[0] = recv_protocol.person[0];
    			send_protocol.person[1] = recv_protocol.person[1];
    			send_protocol.return_value = 0;

    			sprintf(called_filename, "function_dir/%d.so", recv_protocol.person[0]);
    			if(access(called_filename, X_OK)==0){
    				//fprintf(stderr, "- file: %s exist\n", called_filename);
	    			sprintf(dlopen_name, "./%s", called_filename);
					handle = dlopen(dlopen_name, RTLD_LAZY);
					int (*filter1)(User) = (int(*)(User))dlsym(handle, "filter_function");
					return1 = filter1(recv_protocol.person_data[1]);
					//fprintf(stderr, "- after %s, return_value is %d\n", called_filename, send_protocol.return_value);
					dlclose(handle);
    			}
    			else	fprintf(stderr, "- worker %d, file: %s does not exist\n", worker_number, called_filename);

    			sprintf(called_filename, "function_dir/%d.so", recv_protocol.person[1]);
    			if(access(called_filename, X_OK)==0){
    				//fprintf(stderr, "- file: %s exist\n", called_filename);
	    			sprintf(dlopen_name, "./%s", called_filename);
					handle = dlopen(dlopen_name, RTLD_LAZY);
					int (*filter2)(User) = (int(*)(User))dlsym(handle, "filter_function");
					return2 = filter2(recv_protocol.person_data[0]);
					//fprintf(stderr, "- after %s, return_value is %d\n", called_filename, send_protocol.return_value);
					dlclose(handle);
    			}
    			else	fprintf(stderr, "- worker %d, file: %s does not exist\n", worker_number, called_filename);

    			if(return1 == 1 && return2 == 1)
    				send_protocol.return_value = 1;
    			else
    				send_protocol.return_value = 0;
    			//fprintf(stderr, "- worker %d get answer %d\n", worker_number, send_protocol.return_value);
    			retval = write(output_fd, &send_protocol, sizeof(send_protocol));
    			if(retval<0) fprintf(stderr, "worker_number %d send protocol error.\n", worker_number);
    		}
    		else fprintf(stderr, "- worker_number %d receive protocol error.\n", worker_number);
    	}
    	else fprintf(stderr, "- worker_number %d select error\n", worker_number);
    }
}

void compare_with_waiting_fd(int *fd){
	//fprintf(stderr, "In the compare_with_waiting_fd. waiting_fd_num == %d.\n", waiting_fd_num);
	int i, j, retval, return_num, return_max;
	int *index=(int*)malloc(sizeof(int));
	pid_t break_pid;
	
	myProtocol recv_protocol, send_protocol;
	send_protocol.person[0] = *fd;
	strcpy(send_protocol.person_data[0].name, user_table[*fd]->name);
	send_protocol.person_data[0].age = user_table[*fd]->age;
	strcpy(send_protocol.person_data[0].gender, user_table[*fd]->gender);
	strcpy(send_protocol.person_data[0].introduction, user_table[*fd]->introduction);
	for(i=0 ; i<waiting_fd_num ; i+=5){
		for(j=0;j<5;j++){
			if(i+j < waiting_fd_num){
				send_protocol.person[1] = waiting_fd[i+j];
				strcpy(send_protocol.person_data[1].name, user_table[waiting_fd[i+j]]->name);
				send_protocol.person_data[1].age = user_table[waiting_fd[i+j]]->age;
				strcpy(send_protocol.person_data[1].gender, user_table[waiting_fd[i+j]]->gender);
				strcpy(send_protocol.person_data[1].introduction, user_table[waiting_fd[i+j]]->introduction);
				
				//fprintf(stderr, "assign worker_number %d job, %d and %d\n", j, *fd, waiting_fd[i+j]);
				if(write(worker_input_pipe_fd[j], &send_protocol, sizeof(send_protocol)) < 0)
					fprintf(stderr, "write to worker_number %d error\n", j);
				//fprintf(stderr, "write finished to worker_number %d\n", j);
				return_max = j;
			}
			else
				break;
		}
		//fprintf(stderr, "one term job has assigned, return_max = %d.\n", return_max);
		return_num = -1;
		*index = 1030;
		//fprintf(stderr, "ok1\n");
		while(return_num != return_max){
			//fprintf(stderr, "ok2\n");
			for(j=0;j<5;j++){
				break_pid = waitpid(pid[j], NULL, WNOHANG);
				//fprintf(stderr, "%d\n", break_pid);
				if(break_pid == pid[j]){
					fprintf(stderr, "worker %d down, reboot.\n", j);
					return_num ++;
					FD_CLR(worker_output_pipe_fd[j], &readset_forMatch);
					close(worker_input_pipe_fd[j]);
					close(worker_output_pipe_fd[j]);
					char buffer[50];
					sprintf(buffer, "input_pipe_%d", j);
					remove(buffer);
					mkfifo(buffer, 0644);
					sprintf(buffer, "output_pipe_%d", j);
					remove(buffer);
					mkfifo(buffer, 0644);
					//fprintf(stderr, "worker %d has open all pipe\n", worker_number);
					pid[j] = fork();
					if(pid[j]==0) worker(j);
					else if(pid[j]>0){
						sprintf(buffer, "input_pipe_%d", j);
						worker_input_pipe_fd[j] = open(buffer, O_WRONLY);
			    		if(worker_input_pipe_fd[j]<0) fprintf(stderr, "server open worker %d input pipe error\n", j);
						
						sprintf(buffer, "output_pipe_%d", j);
						worker_output_pipe_fd[j] = open(buffer, O_RDONLY);
			    		if(worker_output_pipe_fd[j] < 0) fprintf(stderr, "server open worker %d output pipe error\n", j);
			    		if(worker_output_pipe_fd[j] > maxfd_forMatch)
			    			maxfd_forMatch = worker_output_pipe_fd[j];
						FD_SET(worker_output_pipe_fd[j], &readset_forMatch);
					}
				}
				//else
					//fprintf(stderr, "pid %d not down\n", j);
			}
			memcpy(&working_readset_forMatch, &readset_forMatch, sizeof(readset_forMatch));
			retval = select(maxfd_forMatch+1, &working_readset_forMatch, NULL, NULL, 0);
			//fprintf(stderr, "ok3\n");
			if(retval > 0){
				//fprintf(stderr, "ok4\n");
				for(j=0;j<5;j++){
					//fprintf(stderr, "ok5\n");
					if(FD_ISSET(worker_output_pipe_fd[j], &working_readset_forMatch)){
						return_num ++;
						retval = read(worker_output_pipe_fd[j], &recv_protocol, sizeof(recv_protocol));
						if(retval < 0)	fprintf(stderr, "compare_with_waiting_fd read error\n");
						//fprintf(stderr, "%d and %d return_value is %d\n", recv_protocol.person[0], recv_protocol.person[1], recv_protocol.return_value);
						if(recv_protocol.return_value == 1 && recv_protocol.person[1] < *index){
							*index = recv_protocol.person[1];
						}
					}
				}
			}
			else if(retval<0)	fprintf(stderr, "compare_with_waiting_fd select error\n");
		}
		if(*index != 1030){
			pthread_exit((void*)index);
		}
	}
	*index = *fd;
	pthread_exit((void*)index);
}

void creat_file(int fd){
	
	//fprintf(stderr, "in the creat_file\n");
	
	int retval;
	sprintf(filename, "function_dir/%d.c", fd);
	//fprintf(stderr, "filename => %s\n", filename);

	int program_fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0744);
	if(program_fd<0)		fprintf(stderr, "open error\n");
	sprintf(program, "struct User{\nchar name[33];\nunsigned int age;\nchar gender[7];\nchar introduction[1025];\n};\n%s", filter_table[fd]);
	//fprintf(stderr, "in the function\n==\n%s\n==\n", program);
	retval = write(program_fd, program, strlen(program));
	if(retval<0)	fprintf(stderr, "write error\n");
	close(program_fd);
	sprintf(filename, "function_dir/%d.so", fd);
	remove(filename);
	sprintf(system_cmd, "gcc -fPIC -O2 -std=c11 function_dir/%d.c -shared -o function_dir/%d.so", fd, fd);
	//fprintf(stderr, "execute command : %s\n", system_cmd);
	system(system_cmd);
	retval = access(filename, X_OK);
	if(retval<0)	fprintf(stderr, "%s lib gcc error\n", filename);
	else 			//fprintf(stderr, "%s lib gcc success\n", filename);
	return;
}

void takeout_from_waiting(int fd){
	int i, j;
	for(i = 0 ; i < waiting_fd_num ; i++)
		if(waiting_fd[i]==fd){
			for(j = i ; j < waiting_fd_num - 1 ; j++)
				waiting_fd[j]=waiting_fd[j+1];
			waiting_fd_num --;
			break;
		}
	//fprintf(stderr, "waiting_fd_num is %d\n", waiting_fd_num);
	return;
}

void send_json_back(int fd1, int fd2, char *recv_json_string, int status){
	cJSON *send_json;
	char *send_json_string;
	send_json = cJSON_CreateObject();
	if(status == 0){	//try_match
		cJSON_AddStringToObject(send_json, "cmd", "try_match");
	}
	else if(status == 1){	//match
		cJSON_AddStringToObject(send_json, "cmd", "matched");
		cJSON_AddStringToObject(send_json, "name", user_table[fd2]->name);
		cJSON_AddNumberToObject(send_json, "age", user_table[fd2]->age);
		cJSON_AddStringToObject(send_json, "gender", user_table[fd2]->gender);
		cJSON_AddStringToObject(send_json, "introduction", user_table[fd2]->introduction);
		cJSON_AddStringToObject(send_json, "filter_function", filter_table[fd2]);
	}
	else if(status == 2){	//send_message
		send(fd1, recv_json_string, fd2, 0);
		send(fd1, "\n", 1, 0);
		return;
	}
	else if(status == 3){	//reveive_message
		cJSON *root;
		root = cJSON_Parse(recv_json_string);
		cJSON_AddStringToObject(send_json, "cmd", "receive_message");
		cJSON_AddStringToObject(send_json, "message", cJSON_GetObjectItemCaseSensitive(root, "message")->valuestring);
		cJSON_AddNumberToObject(send_json, "sequence", cJSON_GetObjectItemCaseSensitive(root, "sequence")->valueint);
	}
	else if(status == 4){	//quit
		cJSON_AddStringToObject(send_json, "cmd", "quit");
		//free(user_table[fd1]);
		//free(filter_table[fd1]);
	}
	else if(status == 5){	//other_side_quit
		cJSON_AddStringToObject(send_json, "cmd", "other_side_quit");
		//free(user_table[fd1]);
		//free(filter_table[fd1]);
	}
	send_json_string = cJSON_PrintUnformatted(send_json);
	send(fd1, send_json_string, strlen(send_json_string), 0);
	send(fd1, "\n", 1, 0);
	return;
}

int main (int argc, char *argv[]) {
	
	char buffer[6000], recv_data[6000];
	int buffer_len, i, j, retval, recv_data_len;
    int client_fd, temp_fd;
	pthread_t thread_tmp;
	void *match_return;

	memset(sockfd_table, 0, sizeof(sockfd_table));
	mkdir("function_dir", 0777);

	maxfd_forMatch = 0;
	FD_ZERO(&readset_forMatch);
	for(i=0;i<5;i++){
		sprintf(buffer, "input_pipe_%d", i);
		remove(buffer);
		mkfifo(buffer, 0644);
		sprintf(buffer, "output_pipe_%d", i);
		remove(buffer);
		mkfifo(buffer, 0644);
		//fprintf(stderr, "worker %d has open all pipe\n", worker_number);
		pid[i] = fork();
		if(pid[i]==0) worker(i);
		else if(pid[i]>0){
			//fprintf(stderr, "pid %d is %d\n", i, pid[i]);
			sprintf(buffer, "input_pipe_%d", i);
			worker_input_pipe_fd[i] = open(buffer, O_WRONLY);
    		if(worker_input_pipe_fd[i]<0) fprintf(stderr, "server open worker %d input pipe error\n", i);
			
			sprintf(buffer, "output_pipe_%d", i);
			worker_output_pipe_fd[i] = open(buffer, O_RDONLY);
    		if(worker_output_pipe_fd[i] < 0) fprintf(stderr, "server open worker %d output pipe error\n", i);
    		if(worker_output_pipe_fd[i] > maxfd_forMatch)
    			maxfd_forMatch = worker_output_pipe_fd[i];
			FD_SET(worker_output_pipe_fd[i], &readset_forMatch);

			//fprintf(stderr, "server know %d worker is ready.\n", i);
		}
		else	fprintf(stderr, "fork child %d error.\n", i);
	}
	for(i=0;i<1030;i++) match_table[i] = -1;

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0){
		printf("socket fail\n");
		exit(1);
	}
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr)); 			// 清零初始化，不可省略
	server_addr.sin_family = PF_INET;						// 位置類型是網際網路位置
	server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	server_addr.sin_port = htons(atoi(argv[1]));			// 在44444號TCP埠口監聽新連線
	
	if(bind(sockfd, (struct sockaddr*) &server_addr, sizeof(server_addr))<0){
		printf("socket fail\n");
		exit(1);
	}
	listen(sockfd, 1010);

    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    
	FD_ZERO(&readset);
	FD_SET(sockfd, &readset);
	maxfd = sockfd;

	fprintf(stderr, "start loop\n--------------\n");
	while (1){
		//sleep(2);
		memcpy(&working_readset, &readset, sizeof(readset));
		retval = select(maxfd+1, &working_readset, NULL, NULL, 0);
		if(retval>0){
			if(FD_ISSET(sockfd, &working_readset)){		//socket fd
				//fprintf(stderr, "socket get client\n");
				client_fd = accept(sockfd, (struct sockaddr*) &client_addr, &addrlen);
				if(client_fd >= 0){
					FD_SET(client_fd, &readset);
					if(client_fd > maxfd)
						maxfd = client_fd;
					sockfd_table[client_fd] = 1;
					match_table[client_fd] = -1;
					//fprintf(stderr, "get client %d\n--------------\n", client_fd);
				}
				else	fprintf(stderr, "accept error\n--------------\n");
			}
			else{		//other client
				for(temp_fd=3 ; temp_fd <= maxfd ; temp_fd++){
					if(sockfd_table[temp_fd] == 1 && FD_ISSET(temp_fd, &working_readset)){
						//fprintf(stderr, "%d send message\n", temp_fd);
						buffer_len = recv(temp_fd, buffer, 6000, 0);
						//fprintf(stderr, "%d\n", buffer_len);
						if(buffer_len <= 0){
							//fprintf(stderr, "%d quit with no connect.\n", temp_fd);
							if(match_table[temp_fd] != -1){
								//fprintf(stderr, "friend of %d, %d no connect\n", temp_fd, match_table[temp_fd]);
								send_json_back(match_table[temp_fd], 0, NULL, 5);
								match_table[match_table[temp_fd]] = -1;
								match_table[temp_fd] = -1;
							}
							else{
								takeout_from_waiting(temp_fd);
							}
							//free(user_table[temp_fd]);
							//free(filter_table[temp_fd]);
							match_table[temp_fd] = -1;
							sockfd_table[temp_fd] = 0;
							FD_CLR(temp_fd, &readset);
							close(temp_fd);
							//fprintf(stderr, "--------------\n");
							continue;
						}
						memset(recv_data, 0, sizeof(recv_data));
						for(i=0;i<buffer_len;i++){
							recv_data[i] = buffer[i];
							if(buffer[i] == '\n'){
								recv_data[i] = '\0';
								recv_data_len = i;
								break;
							}
						}
						recv_json = cJSON_Parse(recv_data);
						json_cmd = cJSON_GetObjectItemCaseSensitive(recv_json, "cmd");
						//fprintf(stderr, "order is %s\n", json_cmd->valuestring);
						if(strcmp(json_cmd->valuestring, "try_match")==0){
							//fprintf(stderr, "client send try_match\n");
							send_json_back(temp_fd, 0, NULL, 0);

							json_name = cJSON_GetObjectItemCaseSensitive(recv_json, "name");
							json_age = cJSON_GetObjectItemCaseSensitive(recv_json, "age");
							json_gender = cJSON_GetObjectItemCaseSensitive(recv_json, "gender");
							json_introduction = cJSON_GetObjectItemCaseSensitive(recv_json, "introduction");
							json_filter_function = cJSON_GetObjectItemCaseSensitive(recv_json, "filter_function");
							
							user_table[temp_fd] = (User*)malloc(sizeof(User));
							filter_table[temp_fd] = (char*)malloc(strlen(json_filter_function->valuestring)+1);
							
							strcpy(user_table[temp_fd]->name, json_name->valuestring);
							user_table[temp_fd]->age = json_age->valueint;
							strcpy(user_table[temp_fd]->gender, json_gender->valuestring);
							strcpy(user_table[temp_fd]->introduction, json_introduction->valuestring);
							strcpy(filter_table[temp_fd], json_filter_function->valuestring);
							strcat(filter_table[temp_fd], "\0");

							creat_file(temp_fd);
							//matching...
							
							if(waiting_fd_num == 0){
								waiting_fd[waiting_fd_num] = temp_fd;
								waiting_fd_num ++;
							}
							else{
								
								retval = pthread_create(&thread_tmp, NULL, (void*)compare_with_waiting_fd, &temp_fd);
								pthread_join(thread_tmp, &match_return);
								
								//fprintf(stderr, "%d match to %d\n", temp_fd, *(int*)match_return);
								if(*(int*)match_return == temp_fd){
									waiting_fd[waiting_fd_num] = temp_fd;
									waiting_fd_num ++;
									match_table[temp_fd] = -1;
								}
								else{
									match_table[temp_fd] = *(int*)match_return;
									match_table[*(int*)match_return] = temp_fd;
									takeout_from_waiting(*(int*)match_return);
									send_json_back(temp_fd, *(int*)match_return, NULL, 1);
									send_json_back(*(int*)match_return, temp_fd, NULL, 1);
								}
							}
							//fprintf(stderr, "waiting_fd_num is %d\n--------------\n", waiting_fd_num);
						}
						else if(strcmp(json_cmd->valuestring, "quit")==0){
							//fprintf(stderr, "client send quit\n");
							send_json_back(temp_fd, 0, NULL, 4);
							if(match_table[temp_fd] != -1){
								send_json_back(match_table[temp_fd], 0, NULL, 5);
								match_table[match_table[temp_fd]] = -1;
								match_table[temp_fd] = -1;
							}
							else{
								takeout_from_waiting(temp_fd);
								match_table[temp_fd] = -1;
							}
							//fprintf(stderr, "--------------\n");
						}
						else if(strcmp(json_cmd->valuestring, "send_message")==0){
							send_json_back(temp_fd, recv_data_len, recv_data, 2);
							send_json_back(match_table[temp_fd], 0, recv_data, 3);
							//fprintf(stderr, "client send message\n--------------\n");
						}
						else fprintf(stderr, "unexpected cmd\n--------------\n");
					}
				}
			}
		}
	}
	close(sockfd);
	return 0;
}
