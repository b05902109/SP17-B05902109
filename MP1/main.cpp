#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <math.h>
#include <dirent.h>
#include "MD5-friend.c"
char file_name[1010][260], md5_table[1010][33];
char temp_file[1010][260];
int file_name_n;
void open_files(const char *dir_path){
	file_name_n = 0;
	DIR *dir = opendir(dir_path);
	struct dirent *dp;
	while ((dp = readdir(dir)) != NULL) {
	    strcpy(file_name[file_name_n], dp->d_name);
	    file_name_n++;
	}
	closedir(dir);
	return;
}
void mergesort_recursive(int start, int end){
    if(start>=end)
            return;
    int len = end - start, mid = (len>>1) + start;
    int start1 = start, end1 = mid;
    int start2 = mid+1, end2 = end;
    mergesort_recursive(start1, end1);
    mergesort_recursive(start2, end2);
    int k = start;
    while(start1 <= end1 && start2 <= end2){
            if(strcmp(file_name[start1], file_name[start2]) <= 0)
                    strcpy(temp_file[k], file_name[start1++]);
            else
                    strcpy(temp_file[k], file_name[start2++]);
            k++;
    }
    while(start1<=end1){
            strcpy(temp_file[k], file_name[start1++]);
            k++;
    }
	while(start2<=end2){
	     strcpy(temp_file[k], file_name[start2++]);
	     k++;
	}
	for(k = start;k <= end;k++)
	     strcpy(file_name[k], temp_file[k]);
	return;
}
int stack_push(char *stack, char c, int n){
	stack[n] = c;
	return n+1;
}
int stack_popAll(char *string, char *stack, int n){
	int i;
	for(i=n-1;i>=0;i--)
		string[n-1-i] = stack[i];
	string[n] = '\0';
	return 0;
}
int main(int argc, char const *argv[])
{
	FILE *fptr;
	char subName[1010][300], subName_Buffer[600];	//0:status 1:commit 2:log
	char temp_pathname[600];//檔案路徑全名
	int subName_kind[1010], subName_n, command_kind;
	int temp, i, record_index;
	bool has_record = false;
	open_files(argv[argc-1]);
	mergesort_recursive(0, file_name_n-1);
	for (i = 2; i < file_name_n; i++) {
		if( strcmp(file_name[i], ".loser_config")==0 ){	//找出.loser_config與內容
			subName_n = 0;
			strcpy(temp_pathname, argv[argc-1]);
			strcat(temp_pathname, "/");
			strcat(temp_pathname, ".loser_config");
			fptr = fopen(temp_pathname, "r");
			while( fgets(subName_Buffer, 600, fptr)!= NULL){
				temp = 0;
				while( !isspace(subName_Buffer[temp]) ){
					subName[subName_n][temp] = subName_Buffer[temp];
					temp++;
				}
				if(subName_Buffer[temp+3]=='s')
					subName_kind[subName_n] =0;
				else if(subName_Buffer[temp+3]=='c')
					subName_kind[subName_n] = 1;
				else
					subName_kind[subName_n] = 2;
				subName[subName_n][temp] = '\0';
				subName_n++;
			}
			fclose(fptr);
		}
		else if( !has_record && (strcmp(file_name[i], ".loser_record")==0) ){
			has_record = true;
			record_index = i;
		}
	}
	command_kind = 3;
	if(strcmp(argv[1], "status")==0)			//用command_kind紀錄argv[1]
		command_kind = 0;
	else if(strcmp(argv[1], "commit")==0)
		command_kind = 1;
	else if(strcmp(argv[1], "log")==0)
		command_kind = 2;
	else{
		for(i=0;i<subName_n;i++)
			if(strcmp(subName[i], argv[1]) == 0){
				command_kind = subName_kind[i];
				break;
			}
	}
	
	char file_1line[600];

	if(command_kind == 0 || command_kind == 1){		//status 0 commit 1
		char file_name_read[1000][300], md5_table_read[1000][33], c;
		int file_name_read_n = 0;
		for(i=0;i<file_name_n;i++){				//處理目錄中檔案MD5
			if(i != record_index){
				strcpy(temp_pathname, argv[argc-1]);
				strcat(temp_pathname, "/");
				strcat(temp_pathname, file_name[i]);
				MD5(temp_pathname, md5_table[i]);
			}
		}
		strcpy(temp_pathname, argv[argc-1]);
		strcat(temp_pathname, "/.loser_record");
		if(!has_record ){	// commit but no record
			if(command_kind == 1 && file_name_n <= 2)
				return 0;
			if(command_kind){
				freopen(temp_pathname, "w", stdout);
				printf("# commit 1\n");
			}
			printf("[new_file]\n");
			for(i=2;i<file_name_n;i++)
				printf("%s\n", file_name[i]);
			printf("[modified]\n");
			printf("[copied]\n");
			if(command_kind){
				printf("(MD5)\n");
				for(i=2;i<file_name_n;i++){
					printf("%s %s\n", file_name[i], md5_table[i]);
				}
			}
			
		}
		else{
			char stack[300], stack_string[300];
			int stack_n = 0;
			fptr = fopen(temp_pathname, "r");
			fseek(fptr, -2, SEEK_END);
			while( c = fgetc(fptr) ){		//find the md5 of the last commit
				if(c == '#' && stack[stack_n-1] == ' ' && stack[stack_n-2] == 'c'){
					fseek(fptr, -1, SEEK_CUR);
					break;
				}
				if(c == '\n'){
					stack_n = stack_popAll(stack_string, stack, stack_n);
					fseek(fptr, -2, SEEK_CUR);
				}
				else{
					stack_n = stack_push(stack, c, stack_n);
					fseek(fptr, -2, SEEK_CUR);
				}
			}
			fseek(fptr, 9, SEEK_CUR);
			fgets(file_1line, 300, fptr);
			int commit_number = atoi(file_1line);
			while(fgets(file_1line, 600, fptr)){	//"(MD5)\n"
				if(strcmp(file_1line, "(MD5)\n") == 0)
					break;
			}
			while(fgets(file_1line, 300, fptr)!=NULL){	//read the last md5
				bool isNameEnd = false;
				int n = strlen(file_1line), name_len=0;
				for(i=0;i<n;i++){
					if(!isNameEnd && file_1line[i]==' ' )
						isNameEnd = true;
					else if(!isNameEnd){
						file_name_read[file_name_read_n][i] = file_1line[i];
						name_len++;
					}
					else
						md5_table_read[file_name_read_n][i - name_len - 1] = file_1line[i];
				}
				md5_table_read[file_name_read_n][32] = '\0';
				file_name_read_n++;
			}
			fclose(fptr);
			bool has_change = false;
			int record_flag = 0;
			//has_change update......
			if(file_name_n - file_name_read_n != 3)
				has_change = true;
			else{
				for(i=0;i<file_name_read_n && !has_change;i++){
					if(i+2 == record_index)
						record_flag = 1;
					if(strcmp(md5_table[i+2+record_flag], md5_table_read[i])!=0)
						has_change = true;
				}
			}
			//file_name[i] vs file_name_read[i]
			//file_name_n  vs file_name_read_n
			//md5_table[i]      vs md5_table_read[i]
			//i from 2			vs i from 0
			//printf("%d %d\n", file_name_n, file_name_read_n);
			int index = 2, index_read = 0, copy_list[2][1000], copy_list_n = 0;
			if(has_change && command_kind==1){
				freopen(temp_pathname, "a", stdout);
				printf("\n");
				printf("# commit %d\n", commit_number+1);
			}
			if(has_change || command_kind==0){
				printf("[new_file]\n");
				while(index < file_name_n){
					if(index == record_index){
						index++;
					}
					if(index_read >= file_name_read_n || strcmp(file_name[index], file_name_read[index_read])!=0){
						bool iscopy = false;
						for(i=0;!iscopy && i<file_name_read_n;i++)
							if(strcmp(md5_table[index], md5_table_read[i])==0){
								iscopy = true;
								copy_list[0][copy_list_n] = i;
								copy_list[1][copy_list_n] = index;
								copy_list_n++;
							}
						if(!iscopy)
							printf("%s\n", file_name[index]);
						index++;
					}
					else{
						index++;
						index_read++;
					}
				}
				index = 2;
				index_read = 0;
				printf("[modified]\n");
				while(index_read < file_name_read_n){
					if(strcmp(file_name[index], file_name_read[index_read])!=0){
						index++;
					}
					else{
						if(strcmp(md5_table[index], md5_table_read[index_read])!=0)
							printf("%s\n", file_name[index]);
						index++;
						index_read++;
					}
				}
				printf("[copied]\n");
				for(i=0;i<copy_list_n;i++){
					printf("%s => %s\n", file_name_read[copy_list[0][i]], file_name[copy_list[1][i]]);
				}
				if(command_kind){
					printf("(MD5)\n");
					for(i=2;i<file_name_n;i++)
						if(i != record_index)
							printf("%s %s\n", file_name[i], md5_table[i]);
				}
			}
		}
	}
	else if(has_record && command_kind == 2){	//log
		//printf("get into log funtion\n");
		char stack[600], stack_string[600], c;
		int stack_n = 0;
		int count_commit_n = atoi(argv[argc-2]), commit_total_c;

		strcpy(temp_pathname, argv[argc-1]);
		strcat(temp_pathname, "/.loser_record");
		fptr = fopen(temp_pathname, "r");
		fseek(fptr, -2, SEEK_END);
		for(i=0;i<count_commit_n;i++){
			//printf("commit last 1 start to track\n");
			while( c = fgetc(fptr) ){
				if(c == '#' && stack[stack_n-1] == ' ' && stack[stack_n-2] == 'c'){
					stack_n = stack_push(stack, c, stack_n);
					fseek(fptr, -1, SEEK_CUR);
					break;
				}
				if(c == '\n'){
					stack_n = stack_popAll(stack_string, stack, stack_n);
					//printf("%s\n", stack_string);
					fseek(fptr, -2, SEEK_CUR);
				}
				else{
					stack_n = stack_push(stack, c, stack_n);
					fseek(fptr, -2, SEEK_CUR);
				}
			}
			stack_n = stack_popAll(stack_string, stack, stack_n);
			
			if(stack_string[9]=='1' && stack_string[10]=='\0')
				i = count_commit_n;
			
			commit_total_c = 0;
			while(fgets(file_1line, 600, fptr)!=NULL){
				if(strcmp(file_1line, "\n")==0){
					commit_total_c += 1;
					break;
				}
				printf("%s", file_1line);
				commit_total_c += strlen(file_1line);
			}
			//printf("%d\n", commit_total_c);
			fseek(fptr, -commit_total_c-1, SEEK_CUR);
			if(i < count_commit_n-1)
				printf("\n");
		}
		//printf("log fuction ends\n");
		fclose(fptr);
	}
	return 0;
}
