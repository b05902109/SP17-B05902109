#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
int main ( int argc, char *argv[] ) {
	bool isExist[100];
	int i, count, lenOfInput;
	char c;
	
	for(i=0;i<100;i++)
		isExist[i] = false;
	lenOfInput=strlen(argv[1]);
	for(i=0;i<lenOfInput;i++)
		isExist[ *(argv[1]+i)-32 ] = true;
		
	if(argc == 3){
		FILE *fptr = freopen(argv[2], "r", stdin);
		if(!fptr){
			printf("error\n");
			return 0;
		}
		else{
			count = 0;
			while(scanf("%c", &c)!=EOF){
				if(c == '\n'){
					printf("%d\n", count);
					count = 0;
				}
				else{
					if( isExist[c-32] )
						count++;
				}
			}
		}
	}
	else{
		count = 0;
		while(scanf("%c", &c)!=EOF){
	        if(c == '\n'){
                printf("%d\n", count);
	  	        count = 0;
  			}
 			else{
        		if( isExist[c-32] )
         		       count++;
 			}
		}
	}
	return 0;
}
