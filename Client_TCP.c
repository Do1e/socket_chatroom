#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/stat.h>

#define MAXLINE 200
#define MAXNAME 20
#define SERV_PORT 8000
#define MAXFILE 10240
#define FINISHFLAG "|_|_|"

struct sockaddr_in servaddr;
char buf[MAXLINE + 50], receivemsg[MAXLINE + 50], filebuf[MAXFILE + 50];
//   发送信息缓存        接送信息缓存               文件信息缓存
//   略加大数组大小，防止输入字符满导致没有结束符'\0'
int sockfd, n;
char IP[INET_ADDRSTRLEN + 5];
int stop = 0;
pthread_t tid;

int isIP(char *IP);			// 判断一个字符串是否是IP地址
void *listening();			// 监听接收信息线程函数
void get_name(int mode);	// 输入姓名
void upload_file();			// 上传文件
void download_file();		// 下载文件
void sendonemsg(char *msg); // 发送消息
void startlistening();		// 开启接收消息线程

int main(){
	// 输入IP，若为空则使用默认IP
	// 不为空判断是否为正确IP地址，直到输入正确
	printf("输入服务器IP地址(default: 172.26.120.220):\n");
	fgets(IP, INET_ADDRSTRLEN + 5, stdin);
	IP[strlen(IP)-1] = 0; // 过滤回车
	while(!(isIP(IP))){
		if(strlen(IP) == 0){
			strcpy(IP, "172.26.120.220");
			break;
		} // 空白输入，使用默认IP
		printf("错误IP地址，请重试:\n");
		fgets(IP, INET_ADDRSTRLEN + 5, stdin);
		IP[strlen(IP)-1] = 0;
	}
	IP[strlen(IP)] = 0;
    
	// 连接服务器
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	inet_pton(AF_INET, IP, &servaddr.sin_addr);
	servaddr.sin_port = htons(SERV_PORT);
	if(connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1){
		perror("无法连接到服务器");
		return 0;
	}
	
	startlistening();	// 开启接收消息线程
	stop = 1;
	get_name(0);		// 首次输入姓名
	while(stop);		// 此时在判断是否重名，暂停主函数运行
	printf("输入 ':q' 退出聊天室\n");
	printf("输入 ':r' 改名\n");
	printf("输入 ':s' 显示所有在线用户\n");
	printf("输入 ':f' 显示所有云端文件\n");
	printf("输入 ':u' 上传文件\n");
	printf("输入 ':d' 下载文件\n");

	while(fgets(buf, MAXLINE, stdin) != NULL){
		buf[strlen(buf)-1] = 0;			// 过滤结尾回车
		int quit = 0;
		if(buf[0] == ':'){				// 输入为命令语句
			if(buf[1] == 'q') quit = 1; // 将退出标志置一
			else if(buf[1] == 'r'){
				stop = 1;
				get_name(1);
				while(stop);			// 改名时暂停主函数运行，直至不重名
				memset(buf, 0, sizeof(buf));
				continue;
			}
			else if(buf[1] == 's');		// 这两个功能只需发送命令代码
			else if(buf[1] == 'f');		// 接收线程将服务器发来的数据显示
			else if(buf[1] == 'u'){
				stop = 1;
				upload_file();
				while(stop);			// 上传文件时暂停主函数运行，直至完成上传
				memset(buf, 0, sizeof(buf));
				continue;
			}
			else if(buf[1] == 'd'){
				stop = 1;
				download_file();
				while(stop);			// 下载文件时暂停主函数运行，直至完成下载
				memset(buf, 0, sizeof(buf));
				continue;
			}
			else{						// 非法命令
				printf("此命令不存在\n");
				memset(buf, 0, sizeof(buf));
				continue;
			}
		}
		sendonemsg(buf);				// 发送命令或消息
		// :q :s :f 在此发送，其他命令在各自的函数内处理
		memset(buf, 0, sizeof(buf));
		if(quit == 1) break;
	}
	close(sockfd);
	return 0;
}

int isIP(char *IP){
	int n = strlen(IP);
	int np = 0;
	int num = 0;
	for(int i = 0; i <= n; i++){
		if(IP[i] == '.' || i == n){
			np++;
			if(num > 255) return 0;
			num = 0;
		}else if(IP[i]>='0' && IP[i]<='9')
			num = num * 10 + IP[i] - '0';
		else return 0;
	}
	if(np == 4) return 1;
	else return 0;
}

void startlistening(){
	int ret = pthread_create(&tid, NULL, listening, NULL);
	if(ret != 0){
		printf("Fail to create a new thread.");
		exit(0);
	}
	ret = pthread_detach(tid);
	if(ret != 0) exit(0);
}

void *listening(){
	while(1){
		memset(receivemsg, 0, sizeof(receivemsg));
		n = read(sockfd, receivemsg, MAXLINE);
		if (n <= 0){
			printf("服务器已关闭\n");
			close(sockfd);
			exit(0);
		}
		else
			printf("%s", receivemsg);
		if(receivemsg[0]!='E') stop = 0; // 如果出错，即重名等情况，暂停主函数运行
		if(receivemsg[0]=='E' && receivemsg[6]=='1') exit(0);
		if(receivemsg[0]=='E' && receivemsg[6]=='2') get_name(0);
		if(receivemsg[0]=='E' && receivemsg[6]=='3') get_name(1);
		if(receivemsg[0]=='E' && receivemsg[6]=='4') stop = 0;
		if(receivemsg[0]=='E' && receivemsg[6]=='5') stop = 0;
		// 六种错误代码：
		// 1: 聊天室人满，退出程序
		// 2: 首次输入姓名重名，重新进行输入姓名
		// 3: 改名时姓名重名，重新进行输入姓名
		// 4: 服务器没有成功新建文件，上传失败，主函数继续运行
		// 5: 上传时，服务器中存在相同文件，上传失败，主函数继续运行
		// 6: 下载时，服务器中不存在该文件，下载失败，主函数继续运行
		//    下载文件时关闭了此线程，此错误处理写在下载文件函数内
	}
}

void sendonemsg(char *msg){ write(sockfd, msg, strlen(msg)); }

// 两种模式：0为进入聊天室时命名，1为重命名
void get_name(int mode){
	char name[MAXNAME];
	memset(buf, 0, sizeof(buf));
	printf("输入你的姓名:\n");
	scanf("%s", name); getchar();
	memset(buf, 0, sizeof(buf));
	if(mode == 0) strcat(buf, ":n ");
	if(mode == 1) strcat(buf, ":r ");
	strcat(buf, name); //转换为命令+参数格式发送给服务器处理
	sendonemsg(buf);
}

void upload_file(){
	// 输入文件路径及文件名，以二进制读取方式打开文件
	printf("输入文件路径及文件名(for example ./file):\n");
	char filename[MAXLINE - 10];
	scanf("%s", filename);
	FILE *fp = fopen(filename, "rb");
	if(fp == NULL){ printf("打开文件失败\n"); stop = 0; return; }
	// 退出时需要重新启动主函数的运行
	
	// 计算文件字节数
	struct stat st;
	stat(filename, &st);
	int size = st.st_size;
	int total = 0;

	// 清除路径，只保留文件名，并转换为命令+参数格式发送给服务器
	int nn = strlen(filename) - 1;
	while(nn >= 0 && filename[nn] != '/') nn--;
	while(nn+1){ strcpy(filename, filename+1); nn--; }
	memset(buf, 0, sizeof(buf));
	sprintf(buf, ":u %s", filename);
	sendonemsg(buf);

	usleep(100000); // 等待服务器进行处理
	// 此处判断是否发送错误4或5，出现则不再发送
	if(stop == 0){ fclose(fp); return; }
	memset(filebuf, 0, sizeof(filebuf));
	pthread_cancel(tid); //发送时关闭接收信息线程，防止发送被打断

	// 开始发送文件
	while(nn = fread(filebuf,sizeof(char),MAXFILE,fp)){
		if(nn == 0) break; // 发送结束
		total += nn;       // 累加已发送的字节数
		printf("%6.2f%%", (float)total/(size)*100.0); // 显示已发送的百分比
		//if(stop == 0){ fclose(fp); startlistening(); return; }
		write(sockfd, filebuf, MAXFILE);
		printf("\b\b\b\b\b\b\b"); // 只显示一个进度百分比
		memset(filebuf, 0, sizeof(filebuf)); // 清空缓存，防止产生数据包重叠
	}
	startlistening();	// 发送结束，重新开启接收信息线程
	strcpy(buf, FINISHFLAG);
	usleep(1000000); 	// 等待服务器处理完最后一个数据包后
	sendonemsg(buf);	// 发送结束标志
	stop = 0;			// 主函数继续运行
	fclose(fp);
}

void download_file(){
	pthread_cancel(tid); //下载时关闭接收信息线程，此函数进行接收
	
	// 输入并发送下载命令
	printf("输入服务器上的文件名:\n");
	char filename[MAXLINE - 10];
	scanf("%s", filename);
	memset(buf, 0, sizeof(buf));
	sprintf(buf, ":d %s", filename);
	sendonemsg(buf);
	usleep(10000);
	
	n = read(sockfd, receivemsg, MAXLINE);
	int size = 0;
	// 如果接收到错误信息则打印并退出，同时还需开启接收信息线程
	if(receivemsg[0]=='E' && receivemsg[6]=='6'){
		puts(receivemsg); stop = 0;
		startlistening(); return;
	}
	// 否则接收的就是文件大小信息
	else size = atoi(receivemsg);

	// 以二进制形式写文件
	FILE *fp = fopen(filename, "wb");
	if(fp == NULL){
		printf("打开文件失败\n");
		stop = 0; startlistening(); return;
	}
	int total = 0;
	// 下载中断后删除未下载完成的文件
	char Command[MAXLINE + 10];
	sprintf(Command, "rm -f %s", filename);
	while(1){
		memset(filebuf, 0, sizeof(filebuf));
		printf("%6.2f%%", (float)total/(size)*100.0); // 输出已下载的百分比
		n = read(sockfd, filebuf, MAXFILE);
		printf("\b\b\b\b\b\b\b");					  // 只显示一个进度百分比
		if(n <= 0){									  // 下载中断，删除文件并退出
			printf("服务器已关闭\n");
			fclose(fp);
			system(Command);
			exit(0);
		}
		if(strcmp(filebuf, FINISHFLAG) == 0){		  // 接收到结束标志，下载成功
			printf("下载成功\n");
			break;
		}
		fwrite(filebuf, n, 1, fp);
		fflush(fp);
		total += n;									  // 累加已发送字节数
	}
	startlistening(); 	// 开启接收信息线程
	stop = 0;			// 主函数继续运行
	fclose(fp);
}
