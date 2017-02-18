#include "clifunc.h"


static int epollfd;
int sockfd;
struct sockaddr_in servaddr;
struct sockaddr_in localaddr;		//本地内网地址
struct sockaddr_in outeraddr;		//NAT设备转发的地址
struct epoll_event ev,events[cliMAX_EVENTS];
map<string,struct user> usermap;
struct msghdr msgsend, msgrecv;		//同一时间不存在多个线程操作msgsend和msgrecv，所以可设为全局静态变量
struct iovec iovsend[3], iovrecv[2];
char name[namelen+2];				//本地登录名
string string_name;
pthread_mutex_t maplock = PTHREAD_MUTEX_INITIALIZER;



void* thread_heart(void* arg)
{
	char control[commandlen] = "heart";
	char message[commandlen+namelen] = {0};
	struct sockaddr_in peeraddr;
	int time_count = 0;
	memcpy(message, control, commandlen);
	memcpy(message+commandlen, name, namelen);
	sleep(5);
	for(;;){
		sendto(sockfd, message, sizeof(message), 0, (struct sockaddr*)&servaddr, sizeof(servaddr));
		++time_count;
		if(time_count > 12){
			map<string, struct user>::iterator it;
			pthread_mutex_lock(&maplock);
			for(it = usermap.begin(); it != usermap.end(); ++it){
				peeraddr = it->second.addr;
				sendto(sockfd, message, 0, 0, (struct sockaddr*)&peeraddr, sizeof(peeraddr));
			}
			pthread_mutex_unlock(&maplock);
			time_count = 0;
		}
		sleep(1);
	}
	return (void*)0;
}

int main(int argc, char **argv)
{
	char control[commandlen] = {0};				//存放控制信息
	char recvline[MAXLINE];
	char input[UDPMAXLINE];		//UDPMAXLINE是576-8-20=548个字节，其实这里再加1也是可以的，因为不会把'\0'发出
	char rest_input[UDPMAXLINE-sizeof(control)];
	int m, nfds;
	ssize_t n;
	pthread_t tid;
	/*	
	struct sigaction SIGALRM_act, SIGINT_act;
	SIGALRM_act.sa_handler = alarm_handler;
	SIGINT_act.sa_handler = interrupt_handler;
	sigemptyset(&SIGALRM_act.sa_mask);
	sigemptyset(&SIGINT_act.sa_mask);
	SIGALRM_act.sa_flags = 0;
	SIGINT_act.sa_flags = 0;
	SIGALRM_act.sa_flags |= SA_RESTART;
	SIGINT_act.sa_flags |= SA_RESTART;
	if(sigaction(SIGALRM, &SIGALRM_act, NULL) < 0)
		err_sys("sigaction error\n");
	if(sigaction(SIGINT, &SIGINT_act, NULL) < 0)
		err_sys("sigaction error\n");
	*/
	servaddr.sin_family      = AF_INET;
	servaddr.sin_port        = htons(PORT-1);
	if(argc != 2)
		err_sys("invalid arg");
	
	if( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		err_sys("socket error");
	printf("sockfd is %d\n",sockfd);

	if(inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0)
		err_sys("inet_pton error");
	
	msgsend.msg_iov = iovsend;
	msgsend.msg_iovlen = 3;

	inform_server();				//第一次连接服务器

	if(pthread_create(&tid, NULL, &thread_heart, NULL)!= 0)
			err_sys("thread create failed");

	msgrecv.msg_name = NULL;
	msgrecv.msg_namelen = 0;
	msgrecv.msg_iov = iovrecv;
	msgrecv.msg_iovlen = 2;
	iovrecv[0].iov_base = control;
	iovrecv[0].iov_len = sizeof(control);
	iovrecv[1].iov_base = recvline;
	iovrecv[1].iov_len = sizeof(recvline);
	

	if( (epollfd = epoll_create(cliMAX_EVENTS)) == -1)			//注册事件
		err_sys("epoll_create error");
	ev.events = EPOLLIN;
	ev.data.fd = sockfd;
	if(epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &ev) == -1)
		err_sys("epoll_ctl error");
	ev.data.fd = STDIN_FILENO;	
	if(epoll_ctl(epollfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) == -1)
		err_sys("epoll_ctl error");
	
	for( ; ; ){
		if( (nfds = epoll_wait(epollfd, events, cliMAX_EVENTS, -1)) == -1){				//考虑被信号中断的情况
			if(errno == EINTR)
				continue;
			err_sys("epoll_wait error");
		}
		for(m = 0; m < nfds; ++m){
			if(events[m].data.fd == STDIN_FILENO){							//从标准输入读
				if(events[m].events & EPOLLIN){
					if(fgets(input, UDPMAXLINE, stdin) == NULL ){
						printf("input finish\n");
						return 1;
					}
					bzero(control, sizeof(control));
					bzero(rest_input, sizeof(rest_input));
					if(parseInput(input, control, sizeof(control), rest_input)< 0){
						printf("invalid input\n");
						continue;
					}
					printf("control message is ***%s***,rest message is ***%s***\n",control,rest_input);
					operate(control, sizeof(control), rest_input, strlen(rest_input));		
				}
				
			}
			else if(events[m].data.fd == sockfd){							//从套接字读
				if(events[m].events & EPOLLIN){
					bzero(control, sizeof(control));
					bzero(recvline, sizeof(recvline));			//每次都要把接收缓冲区全部置0
					if( (n = recvmsg(sockfd, &msgrecv, 0)) < 0)
						err_sys("recvmsg error");
					if(n == 0)
						continue;
					printf("recvmsg receive %ld bytes from sockfd\n",n);
					printf("recvmsg receive %s control message\n",control);
					parseRecv(n - sizeof(control), control, recvline);			//数据字节数,故要减去控制字段字节
				}
			}
		}
	}


	return 0;
}










void file_request(char* control, size_t len1, char* mes, size_t len2)
{
	char *pos = NULL;
	int onlinenums = usermap.size();
	int fd;
	char peername[namelen] ={0};
	struct file myfile;
	struct stat buf;
	struct sockaddr_in peeraddr;
	struct sockaddr_in alteraddr;
	if(onlinenums == 0){
		printf("none is online\n");
		return;
	}
	if((pos = strchr(mes,' ')) == NULL){
		printf("unknown control message\n");	
		return;
	}
	bzero(&myfile,sizeof(myfile));	
	memcpy(myfile.fileowner, name, namelen);
	memcpy(peername, mes, pos-mes);
	string sname(peername);
	map<string,struct user>::iterator it = usermap.find(sname);
	if(it == usermap.end()){
		printf("not such user\n");
		return;
	}
	memcpy(myfile.fileaddr,pos+1,strlen(pos+1));
	/*if(access(myfile.filename,F_OK) != 0){
		printf("no such file\n");
		return;
	}*/
	if( (fd = open(myfile.fileaddr, O_RDONLY)) < 0){		//打开想要传输的文件,之后要记得关闭
		printf("no such file\n");
		return;
	}
	myfile.fd = fd;
	if( fstat(fd, &buf) < 0)			//获取文件的stat结构，里面有文件大小信息
		err_sys("fstat error");
	myfile.filesize = buf.st_size;
	printf("file size is %lu, fd is %d\n",myfile.filesize, fd);
	msgsend.msg_namelen = sizeof(peeraddr);	
	msgsend.msg_iovlen = 2;
//	iovsend[0].iov_base = control;
	iovsend[0].iov_len = len1;
	//iovsend[1].iov_base = name;
	//iovsend[1].iov_len = namelen;
	iovsend[1].iov_base = &myfile;
	iovsend[1].iov_len = sizeof(myfile);
	peeraddr = it->second.addr;
	if(outeraddr.sin_addr.s_addr == peeraddr.sin_addr.s_addr || outeraddr.sin_addr.s_addr == localaddr.sin_addr.s_addr){	
		//发送端和本机处于同一局域网或发送端处于公网,此情况会直接开始监听，并将监听端口发送到对端
		struct file_arg *my_arg = (struct file_arg*)malloc(sizeof(struct file_arg));		
		int listenfd;
		char str[6] = {0};
		struct sockaddr_in bindaddr;
		socklen_t len = sizeof(bindaddr);
		bzero(&bindaddr,sizeof(bindaddr));
		bindaddr.sin_family = AF_INET;
		bindaddr.sin_addr.s_addr = htonl(INADDR_ANY);
		bindaddr.sin_port = htons(0);
		if( (listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
			err_sys("create listenfd error\n");
		my_arg->flag = 1;
		my_arg->listenfd = listenfd;
		my_arg->fd = fd;
		memcpy(&(my_arg->myfile), &myfile, sizeof(myfile));
		if(bind(listenfd, (struct sockaddr*) &bindaddr, sizeof(bindaddr)) < 0)
			err_sys("bind error\n");
		if(getsockname(listenfd, (struct sockaddr*) &bindaddr, &len) < 0)
			err_sys("getsockname failed\n");

	

		printf("tcpfd is %d\n",listenfd);
		if(outeraddr.sin_addr.s_addr == peeraddr.sin_addr.s_addr)
			alteraddr = it->second.inner_addr;
		else
			alteraddr = it->second.addr;
		myfile.addr = localaddr;			//无论发送端与接收端处于同一局域网还是发送端处于公网，发送端能访问到的地址都是localaddr
		sprintf(str, "%d", bindaddr.sin_port);
		strcat(control,str);
		printf("after strcat, the control is %s\n",control);
		iovsend[0].iov_base = control;
		msgsend.msg_name = (struct sockaddr*)&alteraddr;
		if(listen(listenfd, 5) < 0)
			err_sys("listen error 1\n");
		pthread_create(&myfile.thread_id, NULL, thread_listen, (void*)my_arg);
	}
	else{							//本机在另外一个局域网中
		//未写完
		iovsend[0].iov_base = control;
		myfile.addr = outeraddr;
		msgsend.msg_name = (struct sockaddr*)&peeraddr;	
	}
	if(sendmsg(sockfd, &msgsend, 0) < 0)
		err_sys("sendmsg to server error");
	return;

}







	
void parseRecv(ssize_t n, char *control, char *recvline)
{
	if(strcmp(control,"login") == 0 || strcmp(control,"update") == 0){
		update_map(n);
	}
	else if(strcmp(control,"chatall") == 0 || strcmp(control,"chat") == 0){	//iovrecv[1]的前12个字节是发送端的名字
		char fromwho[namelen];
		memcpy(fromwho, recvline, namelen);
		printf("%s:%s\n",fromwho,(char*)iovrecv[1].iov_base + namelen);
	}
	else if(strcmp(control,"file") == 0){					//此情况下对端不可被直接连接到
		struct file peerfile;
		struct sockaddr_in peeraddr;
		char reply[12] ={0};
		memcpy(&peerfile, recvline, sizeof(peerfile));
		if(strcmp(peerfile.fileowner,name) == 0 ){			//此情况说明自己是文件传输的发起方，并且两方都无法被连接，此时需要进行tcp打洞
		
		
		}
		else {
			printf("%s want to send you a file named \"%s\", yes or no\n", peerfile.fileowner, peerfile.fileaddr);
			fgets(reply,12,stdin);
			reply[strlen(reply)-1] = '\0';
//			string s(peerfile.fileowner);
//			map<string, struct user>::iterator it = usermap.find(s);
//			if(it == usermap.end()){
//				printf("shouldn't be here\n");
//				return;
//			}
			peeraddr = peerfile.addr;
			if(strcmp(reply, "yes") == 0){				
				if(outeraddr.sin_addr.s_addr == localaddr.sin_addr.s_addr){				//本端处于公网
					struct file_arg *my_arg = (struct file_arg*)malloc(sizeof(struct file_arg));		
					int listenfd;
					char str[6] = {0};
					struct sockaddr_in bindaddr;
					peerfile.addr = outeraddr;			//这里要修改addr，给对端连接自己用
					socklen_t len = sizeof(bindaddr);
					bzero(&bindaddr,sizeof(bindaddr));
					bindaddr.sin_family = AF_INET;
					bindaddr.sin_addr.s_addr = htonl(INADDR_ANY);
					bindaddr.sin_port = htons(0);
					if( (listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
						err_sys("create listenfd error\n");
					my_arg->flag = 2;								//很关键，表明该线程是接收端
					my_arg->listenfd = listenfd;
					if(bind(listenfd, (struct sockaddr*) &bindaddr, sizeof(bindaddr)) < 0)
						err_sys("bind error\n");
					if(getsockname(listenfd, (struct sockaddr*) &bindaddr, &len) < 0)
						err_sys("getsockname failed\n");
					printf("tcpfd is %d\n",listenfd);
					sprintf(str, "%d", bindaddr.sin_port);
					strcat(control, str);
					printf("after strcat, the control is %s\n",control);
					if(listen(listenfd, 5) < 0)
						err_sys("listen error 2\n");
					pthread_t ptid;
					pthread_create(&ptid, NULL, thread_listen, (void*)my_arg);
					msgsend.msg_iovlen = 2;
					iovsend[0].iov_base = control;
					iovsend[0].iov_len = commandlen;
					iovsend[1].iov_base = &peerfile;
					iovsend[1].iov_len = sizeof(peerfile);
					msgsend.msg_name = (struct sockaddr*)&peeraddr;	
					if(sendmsg(sockfd, &msgsend, 0) < 0)
						err_sys("sendmsg to server error");

				}
			}
			else{					//倘若拒绝接收
				char refuse[commandlen] = "nofile2";
				int fd = peerfile.fd;
				msgsend.msg_iovlen = 2;
				iovsend[0].iov_base = refuse;
				iovsend[0].iov_len = commandlen;
				iovsend[1].iov_base = &fd;
				iovsend[1].iov_len = sizeof(int);
				msgsend.msg_name = (struct sockaddr*)&(peerfile.addr);	
				if(sendmsg(sockfd, &msgsend, 0) < 0)
					err_sys("sendmsg error1");
			}
		}
	}
	else if(strncmp(control,"file",4) == 0){			//此情况下对端判断自己可以被连接到，已经处于监听状态
		struct file peerfile;
		struct sockaddr_in listenaddr;
		char reply[12] ={0};
		int port;
		char str[6] = {0};
		memcpy(&peerfile,recvline, sizeof(peerfile));
		listenaddr = peerfile.addr;
		if(strcmp(peerfile.fileowner,name) == 0 ){			//此情况说明自己是文件传输的发起方，并且对端可以被连接
				struct file_arg *my_arg = (struct file_arg*)malloc(sizeof(struct file_arg));
				memcpy(&(my_arg->myfile), &peerfile, sizeof(peerfile));
				my_arg->flag = 1;
				my_arg->fd = peerfile.fd;
				memcpy(str, control+4, strlen(control+4));
				port = atoi(str);
				listenaddr.sin_port = port;				//listenaddr与peerfile.addr的端口号不一样
				my_arg->peeraddr = listenaddr;
				pthread_t tid;
				pthread_create(&tid, NULL, thread_connect, (void*)my_arg);
		}
		else{
			printf("%s want to send you a file named \"%s\", yes or no\n", peerfile.fileowner, peerfile.fileaddr);
			fgets(reply,12,stdin);
			reply[strlen(reply)-1] = '\0';
			if(strcmp(reply, "yes") == 0){
				struct file_arg *my_arg = (struct file_arg*)malloc(sizeof(struct file_arg));
				my_arg->flag = 2;
				memcpy(str, control+4, strlen(control+4));
				port = atoi(str);
				/*if(peeraddr.sin_addr.s_addr == outeraddr.sin_addr.s_addr){	//在同一个局域网
					tempaddr = it->second.inner_addr;
					tempaddr.sin_port = port;
				}
				else{
					tempaddr = peeraddr;
					tempaddr.sin_port = port;
				}*/
				listenaddr.sin_port = port;				//listenaddr与peerfile.addr的端口号不一样
				my_arg->peeraddr = listenaddr;
				pthread_t tid;
				pthread_create(&tid, NULL, thread_connect, (void*)my_arg);
			}
			else{
				char refuse[commandlen] = "nofile1";
				pthread_t pth = peerfile.thread_id;
				//printf("peer thread id is %lu\n", pth);
				msgsend.msg_iovlen = 2;
				iovsend[0].iov_base = refuse;
				iovsend[0].iov_len = commandlen;
				iovsend[1].iov_base = &pth;
				iovsend[1].iov_len = sizeof(pthread_t);
			
			/*	if(outeraddr.sin_addr.s_addr == peeraddr.sin_addr.s_addr){	//发送端和本机处于同一局域网
					tempaddr = it->second.inner_addr;
					msgsend.msg_name = (struct sockaddr*)&tempaddr;
				}
				else{
					msgsend.msg_name = (struct sockaddr*)&peeraddr;	
				}*/
				msgsend.msg_name = (struct sockaddr*)&(peerfile.addr);	
				if(sendmsg(sockfd, &msgsend, 0) < 0)
					err_sys("sendmsg error 2");
	
			}
		}
	}
	/*else if(strcmp((char*)iovrecv[0].iov_base,"try") == 0){
		printf("unexpected try control\n");		
		//control字段为try表示udp打洞消息不过应该永远都收不到这个消息才对
	}*/
	else if(strcmp(control,"nofile1") == 0){
		pthread_t thread_id;
		memcpy(&thread_id,recvline, sizeof(pthread_t));
		printf("my thread id is %lu\n", thread_id);
		if(pthread_cancel(thread_id) < 0)
			err_sys("pthread_cancel failed\n");
	}
	else if(strcmp(control,"nofile2") == 0){
		int fd;
		memcpy(&fd, recvline, sizeof(int));
		printf("已经打开的文件描述符是%d\n",fd);
		close(fd);
	}
	else if(strcmp(control,"offline") == 0)
		exit(2);
	else if(strcmp(control,"delete") == 0)
		delete_user(recvline);
	else if(strcmp(control,"add") == 0)
		add_user();	
//	else
//		printf("unknown receive\n");
}

void update_map(ssize_t n)
{
	unsigned long int num = n/sizeof(struct user);
	struct user tempuser;
	unsigned int i;
	char control[commandlen] = "try";
	printf("n is %lu and the num of user is %lu\n",n,num);
	std::pair< map<string, struct user>::iterator,bool> ret;	//用来判断是否插入成功，如果插入成功，说明是新用户，则需要发送udp打洞探测包
	pthread_mutex_lock(&maplock);
	for(i = 0; i < num; i++){
		memcpy(&tempuser, (char*)iovrecv[1].iov_base+i*sizeof(struct user), sizeof(struct user));
		string stemp(tempuser.name);
		if(stemp != string_name){				//用户列表不用加入自己
			ret = usermap.insert(map<string, struct user>::value_type(stemp,tempuser));			
			if(ret.second == true){
				//sendto(sockfd, control, sizeof(control), 0, (struct sockaddr*)&tempuser.addr, sizeof(tempuser.addr));
				sendto(sockfd, control, 0, 0, (struct sockaddr*)&tempuser.addr, sizeof(tempuser.addr));
			}
			else
				printf("already exist\n");		
		}
		else if(outeraddr.sin_port == 0) {		//结构体尚未赋值
			outeraddr = tempuser.addr;
		}
	}
	pthread_mutex_unlock(&maplock);
}

void add_user()
{
	struct user tempuser;
	char control[commandlen] = "try";
	memcpy(&tempuser, (char*)iovrecv[1].iov_base, sizeof(struct user));
	string stemp(tempuser.name);
	pthread_mutex_lock(&maplock);
	if(usermap.find(stemp)!=usermap.end())
		usermap.erase(stemp);
	usermap.insert(map<string, struct user>::value_type(stemp,tempuser));
	pthread_mutex_unlock(&maplock);
	//sendto(sockfd, control, sizeof(control), 0, (struct sockaddr*)&tempuser.addr, sizeof(tempuser.addr));		//打洞用	
	sendto(sockfd, control, 0, 0, (struct sockaddr*)&tempuser.addr, sizeof(tempuser.addr));		//打洞用	
	printf("%s is online\n",tempuser.name);
}


void delete_user(char* target_name)
{
	string sname(target_name);
	pthread_mutex_lock(&maplock);
	if(usermap.erase(sname) != 0)
		printf("delete %s successfully\n",target_name);
	pthread_mutex_unlock(&maplock);
	return;
}

void alarm_handler(int signo)
{
	int save_errno = errno;
	errno = save_errno;

}

void interrupt_handler(int signo)
{
	exit(10);

}

void *thread_listen(void *arg)
{
	int connfd, newfd;
	ssize_t n, nread;
	off_t offset, leftbytes;
	char filename[64] = {0};
	char sendbuf[MAXLINE];
	char recvbuf[MAXLINE] = {0};
	struct stat buf;
	struct file myfile;
	struct file_arg *farg = (struct file_arg*)arg;
	pthread_detach(pthread_self());
	pthread_cleanup_push(listen_cleanup, arg);
	printf("socket %d start listen\n",farg->listenfd);
	if( (connfd = accept(farg->listenfd, NULL, NULL)) < 0)
		err_sys("accept error 1\n");

	if(farg->flag == 1){					//文件发送端
		if(write(connfd, &(farg->myfile), sizeof(struct file)) < 0)	//发送文件大小
			err_sys("write error 1");
	
		if( (n = read(connfd, &offset, sizeof(off_t))) < 0)
			err_sys("read error 1"); 
		else if (n == 0)		//说明对面有该文件
			goto end;	
		
	
		if(lseek(farg->fd, offset, SEEK_SET) == -1)
			err_sys("lseek error");
		leftbytes = farg->myfile.filesize - offset;
		while(leftbytes > 0){		//发送文件大小个字节
			if(leftbytes > MAXLINE){
				if( (nread = read(farg->fd, sendbuf, MAXLINE)) < 0)
					err_sys("read error");
			}
			else{
				if( (nread = read(farg->fd, sendbuf, leftbytes)) < 0)
					err_sys("read error");
			}
			leftbytes = leftbytes - nread;
			if(write(connfd, sendbuf, nread) != nread)
				goto end;	//说明对端异常关闭
		}
	}
	else{
		if ((n = read(connfd, &myfile, sizeof(struct file)))> 0){
	
			printf("读成功了%ld个字节\n",n);
			printf("fileaddr为%s\n",myfile.fileaddr);
			printf("文件的大小为%lu\n",myfile.filesize);
		}
		if(strrchr(myfile.fileaddr,'/') != NULL)
			strcpy(filename,strrchr(myfile.fileaddr,'/')+1);
		else
			strcpy(filename,myfile.fileaddr);
		if(access(filename,F_OK) == 0){		//存在
			if( (newfd = open(filename, O_WRONLY|O_APPEND)) < 0)	//打开想要传输的文件
				err_sys("open error");
			if( fstat(newfd, &buf) < 0)			
				err_sys("fstat error");
			offset = buf.st_size;
			if( offset >= myfile.filesize)
				goto end;
			else{
				if(write(connfd, &offset, sizeof(off_t)) != sizeof(off_t))
					goto end;
			}
		}
		else{
			if( (newfd = open(filename,O_RDWR|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH)) < 0)
				err_sys("open error");
			offset = 0;
			write(connfd, &offset, sizeof(off_t));
		}
		leftbytes = myfile.filesize - offset;		//表明还剩多少字节没有发
	
		while(leftbytes > 0){		//接收文件大小个字节
			if(leftbytes > MAXLINE){
				if( (nread = read(connfd, recvbuf, MAXLINE)) < 0)
					err_sys("read error");
				else if(nread == 0)
					goto end;
			}
			else{
				if( (nread = read(connfd, recvbuf, leftbytes)) < 0)
					err_sys("read error");
				else if(nread == 0)
					goto end;
			}
			leftbytes = leftbytes - nread;
			if(write(newfd, recvbuf, nread) != nread)
				err_sys("write error");
		}


	}
end:	close(connfd);
	if(farg->flag != 1)
		close(newfd);
	pthread_cleanup_pop(1);
	pthread_exit((void*)1);
}

void *thread_connect(void *arg)
{
	int connfd, newfd;
	char recvbuf[MAXLINE] = {0};
	char filename[64] = {0};
	char sendbuf[MAXLINE];
	off_t offset, n;
	off_t leftbytes;
	ssize_t nread;
	struct stat buf;
	struct file myfile;
	struct file_arg *farg = (struct file_arg*)arg;
	pthread_detach(pthread_self());
	pthread_cleanup_push(connect_cleanup, arg);
	
	if((connfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		err_sys("create tcp socket error 2\n");
	struct sockaddr_in tcpaddr = ((struct file_arg*)arg)->peeraddr;
	if(connect(connfd, (struct sockaddr*)&tcpaddr, sizeof(struct sockaddr_in)) != 0)
		err_sys("connect error 2\n");
	

	if(farg->flag != 1){				//文件接收端
		if ((n = read(connfd, &myfile, sizeof(struct file)))> 0){
	
			printf("读成功了%ld个字节\n",n);
			printf("fileaddr为%s\n",myfile.fileaddr);
			printf("文件的大小为%lu\n",myfile.filesize);
		}
		if(strrchr(myfile.fileaddr,'/') != NULL)
			strcpy(filename,strrchr(myfile.fileaddr,'/')+1);
		else
			strcpy(filename,myfile.fileaddr);
	
		if(access(filename,F_OK) == 0){		//存在
			if( (newfd = open(filename, O_WRONLY|O_APPEND)) < 0)	//打开想要传输的文件
				err_sys("open error");
			if( fstat(newfd, &buf) < 0)			
				err_sys("fstat error");
			offset = buf.st_size;
			if( offset >= myfile.filesize)
				goto end;
			else{
				if(write(connfd, &offset, sizeof(off_t)) != sizeof(off_t))
					goto end;
			}
		}
		else{
			if( (newfd = open(filename,O_RDWR|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH)) < 0)
				err_sys("open error");
			offset = 0;
			write(connfd, &offset, sizeof(off_t));
		}
		leftbytes = myfile.filesize - offset;		//表明还剩多少字节没有发
	
		while(leftbytes > 0){		//接收文件大小个字节
			if(leftbytes > MAXLINE){
				if( (nread = read(connfd, recvbuf, MAXLINE)) < 0)
					err_sys("read error");
				else if(nread == 0)
					goto end;
			}
			else{
				if( (nread = read(connfd, recvbuf, leftbytes)) < 0)
					err_sys("read error");
				else if(nread == 0)
					goto end;
			}
			leftbytes = leftbytes - nread;
			if(write(newfd, recvbuf, nread) != nread)
				err_sys("write error");
		}
	}
	else{
		if(write(connfd, &(farg->myfile), sizeof(struct file)) < 0)	//发送文件大小
			err_sys("write error 1");
	
		if( (n = read(connfd, &offset, sizeof(off_t))) < 0)
			err_sys("read error 1"); 
		else if (n == 0)		//说明对面有该文件
			goto end;	

		if(lseek(farg->fd, offset, SEEK_SET) == -1)
			err_sys("lseek error");
		leftbytes = farg->myfile.filesize - offset;
		while(leftbytes > 0){		//发送文件大小个字节
			if(leftbytes > MAXLINE){
				if( (nread = read(farg->fd, sendbuf, MAXLINE)) < 0)
					err_sys("read error");
			}
			else{
				if( (nread = read(farg->fd, sendbuf, leftbytes)) < 0)
					err_sys("read error");
			}
			leftbytes = leftbytes - nread;
			if(write(connfd, sendbuf, nread) != nread)
				goto end;	//说明对端异常关闭
		}


	}
	


end:	close(connfd);
	if(farg->flag != 1)
		close(newfd);
	pthread_cleanup_pop(1);
	pthread_exit((void*)1);
	
}

void listen_cleanup(void *arg)
{
	printf("step in listen cleanup\n");
	close(((struct file_arg*)arg)->listenfd);		//这两个套接字必须在这里关闭的原因是listen线程可能会被其他线程取消
	if(((struct file_arg*)arg)->flag == 1)
		close(((struct file_arg*)arg)->fd);
	free(arg);
	return;
}

void connect_cleanup(void *arg)
{
	printf("step in connect cleanup\n");
	if(((struct file_arg*)arg)->flag == 1)
		close(((struct file_arg*)arg)->fd);
	free(arg);
	return;
}
