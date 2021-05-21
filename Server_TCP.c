#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>

#define MAXLINE 200
#define MAXNAME 20
#define SERV_PORT 8000
#define MAXCON (100 + 1) // 最大连接数为100，最后一个用于临时连接
#define MAXFILE 10240
#define FINISHFLAG "|_|_|"

struct sockaddr_in servaddr, cliaddr[MAXCON];
socklen_t cliaddr_len;
int listenfd, connfd[MAXCON], n;
char buf[MAXCON][MAXLINE + 50], spemsg[MAXCON][MAXLINE + 50], filebuf[MAXCON][MAXFILE + 50];
//   发送给除特定用户外的用户     发送给特点用户                  文件信息缓存
//   略加大数组大小，防止输入字符满导致没有结束符'\0'
//   一个都有对应的缓存，防止消息混叠
char str[INET_ADDRSTRLEN];
char names[MAXCON][MAXNAME];
int used[MAXCON], downloading[MAXCON];
//  记录某个ID是否有用户连接 是否正在下载文件

void *TRD(void *arg);					// 每个用户对应一个线程连接
int Process(int ID);					// 对接收的信息进行处理
void sendonemsg(int sockfd, char *msg); // 发送信息给单个用户
void sendmsgtoall(int ID);				// 发送信息给所有用户

int main(){
	// 服务器初始化
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(SERV_PORT);
	bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
	listen(listenfd, 20);
	memset(names, 0, sizeof(names));
	memset(used, 0, sizeof(used));
	memset(downloading, 0, sizeof(downloading));

	// 预先开启最大用户连接数个线程
	pthread_t tids[MAXCON];
	int index[MAXCON];
	for(int i = 0; i < MAXCON-1; i++) index[i] = i;
	for(int i = 0; i < MAXCON-1; i++){	
		int ret = pthread_create(&tids[i], NULL, TRD, &index[i]);
		if(ret != 0){
			printf("thread create failed");
			return 0;
		}
	}

	printf("Accepting connections ...\n");
	while (1) {
		// 读取下一个为占用的ID，作为下一个连接的用户ID
		int nowID = 0;
		for(nowID = 0; nowID < MAXCON-1; nowID++)
			if(!used[nowID]) break;
		cliaddr_len = sizeof(cliaddr[nowID]);
		// 等待连接
		connfd[nowID] = accept(listenfd, (struct sockaddr *)&cliaddr[nowID], &cliaddr_len);
		
		if(nowID >= MAXCON-1){
			// 再次进行判断是否连接数满，因为第一次判断后可能有用户退出
			for(nowID = 0; nowID < MAXCON-1; nowID++)
				if(!used[nowID]) break;
			if(nowID >= MAXCON-1){
				// 服务器连接数满
				memset(spemsg[nowID], 0, sizeof(spemsg[nowID]));
				strcpy(spemsg[nowID], "Error(1): 聊天室人已满。");
				sendonemsg(connfd[nowID], spemsg[nowID]);
				close(connfd[nowID]);
				continue;
			}else{
				// 再次判断后未满，设置对应的ID
				connfd[nowID] = connfd[MAXCON-1];
				cliaddr[nowID].sin_family = cliaddr[MAXCON-1].sin_family;
				cliaddr[nowID].sin_port = cliaddr[MAXCON-1].sin_port;
				cliaddr[nowID].sin_addr.s_addr = cliaddr[MAXCON-1].sin_addr.s_addr;
			}
		}
		used[nowID] = 1; // 该ID有人占用
	}
	return 0;
}

inline void sendonemsg(int sockfd, char *msg){
	strcat(msg, "\n");				// 发送的消息追加一个回车
	write(sockfd, msg, strlen(msg));
}

inline void sendmsgtoall(int ID){
	// 对处理信息来源的用户，发送指定消息
	if(strlen(spemsg[ID]) != 0) sendonemsg(connfd[ID], spemsg[ID]);
	
	for(int i = 0; i < MAXCON-1; i++){
		if(i == ID) continue;
		// 发送给除信息来源的用户（对正在下载文件的用户不发送，否则其文件将出错）
		else if(used[i] && (!downloading[i])) sendonemsg(connfd[i], buf[ID]);
	}
}

inline int Process(int ID){
	char op[20];
	int p = 0, fun = 0;
	if(buf[ID][0] == ':'){
		// 分离命令和参数 命令保存在op 参数保存在buf[ID]
		p = 0;
		memset(op, 0, sizeof(op));
		while(buf[ID][0] != ' ' && buf[ID][0] != 0){
			op[p] = buf[ID][0]; p++;
			char tmp[MAXLINE + 50];//避免未定义行为
			strcpy(tmp, buf[ID]+1);
			strcpy(buf[ID], tmp);
		}
		op[p] = 0;
		while(buf[ID][0] == ' ' && buf[ID][0] != 0){
			char tmp[MAXLINE + 50];
			strcpy(tmp, buf[ID]+1);
			strcpy(buf[ID], tmp);
		}
		
		if(op[1] == 'n'){
			// 新用户登陆，判断是否重名
			// 重名返回错误信息
			for(int i = 0; i < MAXCON-1; i++)
				if(used[i]){
					if(strcmp(names[i], buf[ID]) == 0){
						memset(spemsg[ID], 0, sizeof(spemsg[ID]));
						strcpy(spemsg[ID], "Error(2): 姓名重复，请重试。");
						sendonemsg(connfd[ID], spemsg[ID]);
						return 0;
					}
				}
			// 不重名则发送提示信息
			strcpy(names[ID], buf[ID]);
			sprintf(buf[ID], "%s(%s:%d)进入了聊天室", names[ID],
					inet_ntop(AF_INET, &cliaddr[ID].sin_addr, str, sizeof(str)),
					ntohs(cliaddr[ID].sin_port));
			memset(spemsg[ID], 0, sizeof(spemsg[ID]));
			strcpy(spemsg[ID], "成功进入聊天室");
			sendmsgtoall(ID);
			return 0;
		}

		if(op[1] == 'r'){
			// 改名，判断是否重名
			// 重名返回错误信息
			char newname[MAXNAME];
			for(int i = 0; i < MAXCON-1; i++){
				if(i == ID) continue;
				if(used[i]){
					if(strcmp(names[i], buf[ID]) == 0){
						memset(spemsg[ID], 0, sizeof(spemsg[ID]));
						strcpy(spemsg[ID], "Error(3): 姓名重复，请重试。");
						sendonemsg(connfd[ID], spemsg[ID]);
						return 0;
					}
				}
			}
			// 不重名则发送提示信息
			strcpy(newname, buf[ID]);
			memset(buf[ID], 0, sizeof(buf[ID]));
			sprintf(buf[ID], "%s(%s:%d)改名为%s", names[ID],
					inet_ntop(AF_INET, &cliaddr[ID].sin_addr, str, sizeof(str)),
					ntohs(cliaddr[ID].sin_port), newname);
			memset(names[ID], 0, sizeof(names[ID]));
			strcpy(names[ID], newname);
			memset(spemsg[ID], 0, sizeof(spemsg[ID]));
			strcpy(spemsg[ID], "改名成功");
			sendmsgtoall(ID);
			return 0;
		}

		if(op[1] == 'q'){
			// 用户退出时，给其他所用用户发送提示信息
			memset(buf[ID], 0, sizeof(buf[ID]));
			sprintf(buf[ID], "%s(%s:%d)离开了聊天室", names[ID],
					inet_ntop(AF_INET, &cliaddr[ID].sin_addr, str, sizeof(str)),
					ntohs(cliaddr[ID].sin_port));
			memset(spemsg[ID], 0, sizeof(spemsg[ID]));
			sendmsgtoall(ID);
			memset(names[ID], 0, sizeof(names[ID]));
			return 1; // 给线程函数返回1，用作后续处理
		}

		if(op[1] == 's'){
			// 给请求的用户发送所有用户信息
			memset(spemsg[ID], 0, sizeof(spemsg[ID]));
			strcpy(spemsg[ID], "IP              Port   name");
			sendonemsg(connfd[ID], spemsg[ID]);
			for(int i = 0; i < MAXCON-1; i++)
				if(used[i]){
					memset(spemsg[ID], 0, sizeof(spemsg[ID]));
					sprintf(spemsg[ID], "%-16s%-7d%s",
							inet_ntop(AF_INET, &cliaddr[i].sin_addr, str, sizeof(str)),
							ntohs(cliaddr[i].sin_port), names[i]);
					sendonemsg(connfd[ID], spemsg[ID]);
				}
			memset(spemsg[ID], 0, sizeof(spemsg[ID]));
			return 0;
		}

		if(op[1] == 'f'){
			// 给请求的用户发送服务器端文件
			system("mkdir Files");
			system("ls Files > ./files.txt");
			FILE *filename = fopen("./files.txt", "r");
			memset(spemsg[ID], 0, sizeof(spemsg[ID]));
			while(fgets(spemsg[ID], MAXLINE, filename) != NULL){
				spemsg[ID][strlen(spemsg[ID])-1] = 0;
				sendonemsg(connfd[ID], spemsg[ID]);
			}
			system("rm -f files.txt");
			return 0;
		}

		if(op[1] == 'u'){
			// 服务器端接收文件
			char filename[MAXLINE];
			char filepath[MAXLINE + 60];
			char Command[MAXLINE + 70];
			memset(filename, 0, sizeof(filename));
			memset(filepath, 0, sizeof(filepath));
			memset(Command, 0, sizeof(Command));
			strcpy(filename, buf[ID]);
			system("mkdir Files");
			sprintf(filepath, "./Files/%s", buf[ID]);
			sprintf(Command, "rm -f %s", filepath);

			// 判断是否已存在同名文件，存在则返回错误信息
			FILE *ff = fopen(filepath, "rb");
			if(ff != NULL){
				memset(spemsg[ID], 0, sizeof(spemsg[ID]));
				strcpy(spemsg[ID], "Error(5): 服务器中存在相同名称文件。");
				sendonemsg(connfd[ID], spemsg[ID]);
				fclose(ff);
				return 0;
			}

			FILE *fp = fopen(filepath, "wb");
			if(fp == NULL){
				// 创建文件失败，返回错误信息
				memset(spemsg[ID], 0, sizeof(spemsg[ID]));
				strcpy(spemsg[ID], "Error(4): 上传文件失败，请重试。");
				sendonemsg(connfd[ID], spemsg[ID]);
				return 0;
			}
			// 开始接收文件
			while(1){
				memset(filebuf[ID], 0, sizeof(filebuf[ID]));
				n = read(connfd[ID], filebuf[ID], MAXFILE + 10);
				if (n <= 0) {
					// 接收途中客户端断连，发送提示信息并删除未完全接受的文件
					sprintf(buf[ID], "%s(%s:%d)离开了聊天室", names[ID],
							inet_ntop(AF_INET, &cliaddr[ID].sin_addr, str, sizeof(str)),
							ntohs(cliaddr[ID].sin_port));
					memset(spemsg[ID], 0, sizeof(spemsg[ID]));
					sendmsgtoall(ID);
					close(connfd[ID]);
					memset(names[ID], 0, sizeof(names[ID]));
					used[ID] = 0;
					fclose(fp);
					system(Command);
					return 1;
				}
				if(strcmp(filebuf[ID], FINISHFLAG) == 0){
					// 接收完成，发送相关提示信息
					memset(buf[ID], 0, sizeof(buf[ID]));
					sprintf(buf[ID], "%s(%s:%d)上传了一个文件名为: %s", names[ID],
							inet_ntop(AF_INET, &cliaddr[ID].sin_addr, str, sizeof(str)),
							ntohs(cliaddr[ID].sin_port), filename);
					memset(spemsg[ID], 0, sizeof(spemsg[ID]));
					strcpy(spemsg[ID], "上传成功");
					sendmsgtoall(ID);
					fclose(fp);
					return 0;
				}
				// 写文件
				fwrite(filebuf[ID], n, 1, fp);
	    		fflush(fp);
			}
		}

		if(op[1] == 'd'){
			// 服务器发送文件
			char filename[MAXLINE];
			char filepath[MAXLINE + 60];
			char Command[MAXLINE + 70];
			memset(filename, 0, sizeof(filename));
			memset(filepath, 0, sizeof(filepath));
			memset(Command, 0, sizeof(Command));
			strcpy(filename, buf[ID]);
			sprintf(filepath, "./Files/%s", buf[ID]);
			sprintf(Command, "rm -f %s", filepath);

			FILE *fp = fopen(filepath, "rb");
			if(fp == NULL){
				// 打开文件失败，发送错误信息
				memset(spemsg[ID], 0, sizeof(spemsg[ID]));
				strcpy(spemsg[ID], "Error(6): 文件不存在，可使用:f命令查看文件列表");
				sendonemsg(connfd[ID], spemsg[ID]);
				return 0;
			}

			// 计算文件大小并发送给客户端
			struct stat st; stat(filepath, &st);
			memset(spemsg[ID], 0, sizeof(spemsg[ID]));
			sprintf(spemsg[ID], "%ld", st.st_size);
			write(connfd[ID], spemsg[ID], strlen(spemsg[ID]));
			usleep(10000);
			downloading[ID] = 1; // 当前用户正在下载文件，不再给他发送其他信息

			while(fread(filebuf[ID],sizeof(char),MAXFILE,fp)){
				write(connfd[ID], filebuf[ID], MAXFILE);
				memset(filebuf[ID], 0, sizeof(filebuf[ID]));
			}

			// 发送结束标志
			memset(spemsg[ID], 0, sizeof(spemsg[ID]));
			strcpy(spemsg[ID], FINISHFLAG);
			usleep(1000000); // 等待客户端处理最后一个数据包
			write(connfd[ID], spemsg[ID], strlen(spemsg[ID]));
			// 当前用户未在下载文件
			downloading[ID] = 0;
			fclose(fp);
		}
	}else{ // 非命令，作为消息发送给所有用户，并给消息来源发送提示
		// 获取当前系统时间
		time_t tnow;
		tnow = time(0);
		struct tm *tmnow = localtime(&tnow);		

		if(strlen(buf[ID]) == 0) return 0;
		char temp[MAXLINE]; strcpy(temp, buf[ID]);
		sprintf(buf[ID], "\t%02u:%02u:%02u\n>> %s: %s",tmnow->tm_hour,
			    tmnow->tm_min, tmnow->tm_sec ,names[ID], temp);
		memset(spemsg[ID], 0, sizeof(spemsg[ID]));
		strcpy(spemsg[ID], "发送成功");
		sendmsgtoall(ID);
		return 0;
	}
	return 0;
}

void *TRD(void *arg){
	int ID = *(int *)arg;
	while(1){
		while(!used[ID]);  // 直到当前ID有用户连接才运行
		memset(buf[ID], 0, sizeof(buf[ID]));
		n = read(connfd[ID], buf[ID], MAXLINE);
		if(n <= 0){
			// 当前ID用户断开，向其他所有用户发送提示信息
			sprintf(buf[ID], "%s(%s:%d)离开了聊天室", names[ID],
					inet_ntop(AF_INET, &cliaddr[ID].sin_addr, str, sizeof(str)),
					ntohs(cliaddr[ID].sin_port));
			memset(spemsg[ID], 0, sizeof(spemsg[ID]));
			sendmsgtoall(ID);
			close(connfd[ID]);
			memset(names[ID], 0, sizeof(names[ID]));
			used[ID] = 0; // 当前ID无用户连接
		}
		buf[ID][n] = 0;
		printf("Received from %s at PORT %d: %s\n",
				inet_ntop(AF_INET, &cliaddr[ID].sin_addr, str, sizeof(str)),
				ntohs(cliaddr[ID].sin_port), buf[ID]);
		int q = Process(ID); // 对收到的信息进行处理
		// 返回值为1时表示收到:q命令，该用户离开，关闭连接
		if(q == 1){
			close(connfd[ID]);
			used[ID] = 0;
		}
	}
}
