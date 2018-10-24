#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

using namespace std;
unsigned int smallEnd(const unsigned int input){
	return ((input&0x000000ff)<<24)|((input&0x0000ff00)<<8)|
	((input&0x00ff0000)>>8)|((input&0xff000000)>>24);
}
void add(const char* str);
void changeHex(unsigned int a);

const unsigned K[64] ={
0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};
const int s[64] ={
7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21,
};
unsigned O[4];
unsigned outputer[4] = {0x67452301,0xefcdab89,0x98badcfe,0x10325476};
const unsigned init[4] ={
	0x67452301,0xefcdab89,
	0x98badcfe,0x10325476
};
/*smallEnd(0x67452301),smallEnd(0xefcdab89),
	smallEnd(0x98badcfe),smallEnd(0x10325476)
	*/
void prtFinal(){
	printf("%x %x %x %x\n",smallEnd(outputer[0]),smallEnd(outputer[1])
		,smallEnd(outputer[2]),smallEnd(outputer[3]));
}
void mainLoop(const unsigned int* sep){
	O[0] = outputer[0];
	O[1] = outputer[1];
	O[2] = outputer[2];
	O[3] = outputer[3];
	unsigned F,g,tmp;
	for(int i=0;i<64;i++){
		if(i <16){
			F = (O[1]&O[2])|((~O[1])&O[3]);
			g = i;
		}
		else if(i < 32){
			F = (O[3]&O[1])|(~O[3]&O[2]);
			g = (5*i+1)%16;
		}
		else if(i < 48){
			F = O[1]^O[2]^O[3];
			g = (3*i+5)%16;
		}
		else{
			F = O[2]^(O[1]|(~O[3]));
			g = (7*i)%16;
		}
		
		F += O[0] + K[i] + sep[g];
		O[0] = O[3];
		O[3] = O[2];
		O[2] = O[1];
		O[1] += (F<<s[i]|F>>(32-s[i]));
	}
	outputer[0] += O[0];
	outputer[1] += O[1];
	outputer[2] += O[2];
	outputer[3] += O[3];
	return;
}
int min(int a,int b){return (a<b)?a:b;}
int max(int a,int b){return (a>b)?a:b;}


void cutStr(const char* input ,unsigned int* output ,int strLen,unsigned long long total = 0){
	if(strLen == 0){
		output[0] = 0x00000080;
		for(int i=1;i<16;i++)
			output[i] = 0;
		output[14] += total;
		total = total >> 32;
		output[15] += total;	
		return;
	}
	for(int i=0;i<strLen/4;i++){
		for(int j=3;j>=0;j--){
			output[i] = output[i]<<8;
			output[i] += input[i*4+j]&0x000000ff;
		}
	}
	if( (strLen != 64) ){
		output[strLen/4] = 0x00000080;
		if((strLen)%4 != 0){			
			for(int i=1;i<=strLen%4;i++){
				output[strLen/4] = output[strLen/4]<<8;
				output[strLen/4] += input[strLen-i]&0xff;
			}
		}
		for(int i=strLen/4 + 1;i<16;i++)
				output[i] = 0;
		if(total > 0){
			output[14] += total;
			total = total >> 32;
			output[15] += total;	
		}
	}
	
	return;	
}

void prtInt(const unsigned int* input){
	for(int i=0;i<16;i++){
		printf("output[%d] = %08x\n",i,input[i]);
	}
}

void MD5(const char fileName[],char input[]){
	outputer[0] = 0x67452301;
	outputer[1] = 0xefcdab89;
	outputer[2] = 0x98badcfe;
	outputer[3] = 0x10325476;
	//printf("-------MD5.c start-----------\n");

	FILE *fp;
	char buf[65],filePath[512] = "../../testData/";
	unsigned int sep[16];
	int getNum;
	bool end = false;
	unsigned long long total = 0;
	strcat(filePath,fileName);

	fp = fopen(fileName,"rb");
	buf[0] = '\0';


	while(	(getNum = fread(buf, sizeof(char), 64, fp))	){
		if(getNum <= 56){
			total += getNum*8;
			cutStr(buf,sep,getNum,total);
			mainLoop(sep);
			//prtInt(sep);
			end = true;
		}
		else if(getNum < 64){
			total += getNum*8;
			cutStr(buf,sep,getNum);
			mainLoop(sep);
			//prtInt(sep);
			//printf("last with only total\n");
			cutStr("",sep,0,total);
			mainLoop(sep);
			//prtInt(sep);
			end = true;
		}
		else{
			total += 512;
			cutStr(buf,sep,getNum);
			mainLoop(sep);
		}
		buf[getNum] = '\0';
	}
	if(!end){
		//printf("last with only 80 and total\n");
		cutStr("",sep,0,total);
		mainLoop(sep);
		//prtInt(sep);
	}
	//printf("total = %llu\n",(total));
	
	//prtFinal();
	char dic[17] = "0123456789abcdef";
	unsigned long dealer;
	for(int i=0;i<16;i++){
		input[i*2+1] = dic[outputer[i/4]%16];
		outputer[i/4]>>=4;
		input[i*2] = dic[outputer[i/4]%16];
		outputer[i/4]>>=4;
	}
	input[32] = '\0';
	//printf("string = %s\n",input);
	//printf("---------MD5.c end----------\n");
	
	return;
}
