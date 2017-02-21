#include "clifunc.h"


void err_sys(const char *str)
{
	perror(str);
	exit(1);
}


void inform_server()
{
	char command[commandlen] = {0};
	int c;
	char addr[16] = {0};
	printf("please input your login name\n");
	for(;;){
		fgets(name, sizeof(name), stdin);
		if(strlen(name) > namelen){
			printf("login name too long, input again\n");
			//fflush(stdin);				//不能用fflush清洗输入流
			while((c = getchar()) != '\n' && c != EOF);
		}
		else{
			name[strlen(name)-1] = 0;		//把换行符置0
			break;
		}
	}
	string_name = name;
	std::cout << string_name << "*******" << std::endl;
	strcpy(command,"login");
	printf("your name is %s *** and its length is %lu\n", name, strlen(name));
	printf("connecting ...\n");
	if(sendto(sockfd, "try connect" ,commandlen, 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)	//让系统给该套接字分配一个临时的端口号
		err_sys("sendto failed\n");
	socklen_t len = sizeof(localaddr);
	if(getsockname(sockfd, (struct sockaddr*)&localaddr, &len) < 0)
		err_sys("getsockname failed");
	inet_ntop(AF_INET, &localaddr.sin_addr, addr, 16);
	printf("local port is %s:%hu\n",addr,ntohs(localaddr.sin_port));
	req2serv(command, sizeof(command), name, namelen);			//第四个参数取决于服务器接收端的第二个buf
}


void req2serv(char* control, size_t len1, char* mes, size_t len2)
{
	ssize_t n = 0;
	msgsend.msg_name = (struct sockaddr*)&servaddr;
	msgsend.msg_namelen = sizeof(servaddr);	
	iovsend[0].iov_base = control;
	iovsend[0].iov_len = len1;
	iovsend[1].iov_base = mes;
	iovsend[1].iov_len = len2;
	if(strcmp(control, "login") == 0){		//要发送本地内网地址
		getlocalip();
		printf("local ip is %s:%d\n", inet_ntoa(localaddr.sin_addr),ntohs(localaddr.sin_port));
		iovsend[2].iov_base = &localaddr;
		iovsend[2].iov_len = sizeof(localaddr);
	} 
	if( (n = sendmsg(sockfd, &msgsend, 0)) < 0)
		err_sys("sendmsg to server error");
}


void getlocalip()
{
	struct ifaddrs *ifaddr,*ifa;
	if(getifaddrs(&ifaddr) == -1)
		err_sys("getifaddrs failed");
	for(ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next){
		if(ifa->ifa_addr == NULL)				
			continue;
		if(ifa->ifa_addr->sa_family == AF_INET && (strcmp(ifa->ifa_name,"lo") != 0)){	//保存非环回地址的ipv4地址
			struct sockaddr_in *ipAddr = (struct sockaddr_in *)ifa->ifa_addr;
			localaddr.sin_addr = ipAddr->sin_addr;
		}
	}
	freeifaddrs(ifaddr);
}



//解析键盘输入,-1表示控制字段不正确，0表示正确
int parseInput(char *input, char control[], size_t control_size, char *rest_input)
{
	char *pos = NULL;
	int i;
	if((pos = strchr(input,' ')) != NULL){	
		if((pos-input) > (control_size - 1))
			return -1;
		for(i = 0; input[i]!= ' '; ++i){
			control[i] = input[i];
		}
		control[i] = 0;									//terminal null
		strcpy(rest_input, pos+1);
		rest_input[strlen(rest_input)-1] = 0;
		return 0;
	}
	else{
		if(strlen(input) > control_size)
			return -1;
		input[strlen(input)-1] = 0;					//用空字符覆盖换行符
		strcpy(control, input);			
		return 0;
	}
	return -1;
}


//根据control字段决定发给谁，发什么
void operate(char* control, size_t len1, char* mes, size_t len2)
{
	//if(strcmp(control,"update") == 0){
	//	req2serv(control, len1, mes, len2);
	//}
	if(strcmp(control,"quit") == 0){
		req2serv(control,len1,name,namelen);
		exit(5);
	}
	else if(strcmp(control,"chatall") == 0){
		chat2all(control, len1, mes, len2);
	}
	else if(strcmp(control,"chat") == 0){
		chat2one(control, len1, mes, len2);
	}
	else if(strcmp(control,"file") == 0){
		file_request(control, len1, mes, len2);
	}
	else if(strcmp(control,"show") == 0){
		show_users();
	}
	else if(strcmp(control,"help") == 0){
		printf("command \"show\" will show you which users are online\n");
		printf("command \"chat somebody something\" will send something to a specific one\n");
		printf("command \"chatall something\" will send something to all online users\n");
	//	printf("command \"update\" will request server to update online user list\n");
	}
	else
		printf("unknown control message\n");


}


void chat2one(char* control, size_t len1, char* mes, size_t len2)
{
	char *pos = NULL;
	int onlinenums = usermap.size();
	char peername[12] = {0};
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
	memcpy(peername,mes,pos-mes);
	string sname(peername);
	map<string,struct user>::iterator it = usermap.find(sname);
	if(it == usermap.end()){
		printf("not such user\n");
		return;
	}
	msgsend.msg_namelen = sizeof(peeraddr);
	msgsend.msg_iovlen = 3;	
	iovsend[0].iov_base = control;
	iovsend[0].iov_len = len1;
	iovsend[1].iov_base = name;
	iovsend[1].iov_len = namelen;
	iovsend[2].iov_base = pos+1;
	iovsend[2].iov_len = len2-(pos+1-mes);
	peeraddr = it->second.addr;
	if(outeraddr.sin_addr.s_addr == peeraddr.sin_addr.s_addr){	//发送端和本机处于同一局域网
		alteraddr = it->second.inner_addr;
		msgsend.msg_name = (struct sockaddr*)&alteraddr;
	}
	else{
		msgsend.msg_name = (struct sockaddr*)&peeraddr;	
	}
	if(sendmsg(sockfd, &msgsend, 0) < 0)
		err_sys("sendmsg to server error");
	return;
}




void chat2all(char* control, size_t len1, char* mes, size_t len2)
{	
	int onlinenums = usermap.size();
	char addrstr[16] = {0};
	if(onlinenums == 0){
		printf("none is online\n");
		return;
	}
	struct sockaddr_in peeraddr;
	struct sockaddr_in alteraddr;
	msgsend.msg_namelen = sizeof(peeraddr);	
	msgsend.msg_iovlen = 3;
	iovsend[0].iov_base = control;
	iovsend[0].iov_len = len1;
	iovsend[1].iov_base = name;
	iovsend[1].iov_len = namelen;
	iovsend[2].iov_base = mes;
	iovsend[2].iov_len = len2;
	
	pthread_mutex_lock(&maplock);
	map<string,struct user>::iterator it;
	for(it = usermap.begin(); it != usermap.end(); ++it){	
		peeraddr = it->second.addr;
		if(outeraddr.sin_addr.s_addr == peeraddr.sin_addr.s_addr){	//发送端和本机处于同一局域网
			alteraddr = it->second.inner_addr;
			msgsend.msg_name = (struct sockaddr*)&alteraddr;
			if(inet_ntop(AF_INET,&alteraddr.sin_addr,addrstr,sizeof(addrstr)) == NULL)	//获取客户端的地址
				err_sys("inet_ntop error");
		}
		else{
			msgsend.msg_name = (struct sockaddr*)&peeraddr;	
			if(inet_ntop(AF_INET,&peeraddr.sin_addr,addrstr,sizeof(addrstr)) == NULL)	//获取客户端的地址
				err_sys("inet_ntop error");
		}
		printf("send message to %s: %hu \n",addrstr, ntohs(((struct sockaddr_in*)msgsend.msg_name)->sin_port));	//网络字节序转成主机字节序显示
		if(sendmsg(sockfd, &msgsend, 0) < 0)
			err_sys("sendmsg to server error");	
	}	
	pthread_mutex_unlock(&maplock);
}


void show_users()
{
	int onlinenums = usermap.size();
	if(onlinenums == 0){
		printf("none is online\n");
		return;
	}
	map<string,struct user>::iterator it;
	printf("existing users are: ");

	pthread_mutex_lock(&maplock);
	for(it = usermap.begin(); it!=usermap.end(); ++it){
		printf("%s, ",it->second.name);
	}
	pthread_mutex_unlock(&maplock);
	printf("\n");

}

void sendfile(int connfd, struct file *myfile, int fd)
{
	ssize_t n, nread;
	off_t offset, leftbytes;
	char sendbuf[MAXLINE] = {0};
	if(write(connfd, myfile, sizeof(struct file)) < 0)	//发送文件大小
		err_sys("write error 1");
	
	if( (n = read(connfd, &offset, sizeof(off_t))) < 0)
		err_sys("read error 1"); 
	else if (n == 0)		//说明对面有该文件
		return;		
	if(lseek(fd, offset, SEEK_SET) == -1)
		err_sys("lseek error");
	leftbytes = myfile->filesize - offset;
	while(leftbytes > 0){		//发送文件大小个字节
		if(leftbytes > MAXLINE){
			if( (nread = read(fd, sendbuf, MAXLINE)) < 0)
				err_sys("read error");
		}
		else{
			if( (nread = read(fd, sendbuf, leftbytes)) < 0)
				err_sys("read error");
		}
		leftbytes = leftbytes - nread;
		if(write(connfd, sendbuf, nread) != nread)
			return;	//说明对端异常关闭
	}

}

void recvfile(int connfd, int *newfd)
{
	ssize_t n, nread;
	char filename[64] = {0};
	char recvbuf[MAXLINE] = {0};
	off_t offset, leftbytes;
	struct stat buf;
	struct file myfile;
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
		if( (*newfd = open(filename, O_WRONLY|O_APPEND)) < 0)	//打开想要传输的文件
			err_sys("open error");
		if( fstat(*newfd, &buf) < 0)			
			err_sys("fstat error");
		offset = buf.st_size;
		if( offset >= myfile.filesize)
			return;
		else
			if(write(connfd, &offset, sizeof(off_t)) != sizeof(off_t))
				err_sys("write error");
	}
	else{
		if( (*newfd = open(filename,O_RDWR|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH)) < 0)
			err_sys("open error");
		offset = 0;
		if(write(connfd, &offset, sizeof(off_t)) != sizeof(off_t))
			err_sys("write error");
	}

	leftbytes = myfile.filesize - offset;		//表明还剩多少字节没有发
	while(leftbytes > 0){		//接收文件大小个字节
		if(leftbytes > MAXLINE){
			if( (nread = read(connfd, recvbuf, MAXLINE)) < 0)
				err_sys("read error");
			else if(nread == 0)
				return;
			
		}
		else{
			if( (nread = read(connfd, recvbuf, leftbytes)) < 0)
				err_sys("read error");
			else if(nread == 0)
				return;
		}
		leftbytes = leftbytes - nread;
		if(write(*newfd, recvbuf, nread) != nread)
			err_sys("write error");
	}

}
