#include "clifunc.h"
#include <map>
#include <string>
#include <iostream>
using std::string;
using std::map;
static int epollfd;
static int sockfd;
struct sockaddr_in servaddr;
struct sockaddr_in localaddr;		//本地内网地址
struct sockaddr_in outeraddr;		//路由转发的地址
struct epoll_event ev,events[cliMAX_EVENTS];
map<string,struct user> usermap;
static struct msghdr msgsend, msgrecv;		//同一时间不存在多个线程操作msgsend和msgrecv，所以可设为全局静态变量
static struct iovec iovsend[3], iovrecv[2];
static char name[namelen+2];				//本地登录名
static string string_name;



void* thread_heart(void* arg)
{
	char control[commandlen] = "heart";
	char message[commandlen+namelen] = {0};
	memcpy(message, control, commandlen);
	memcpy(message+commandlen, name, namelen);
	sleep(1);
	for(;;){
		sendto(sockfd, message, sizeof(message), 0, (struct sockaddr*)&servaddr, sizeof(servaddr));
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
	
	//alarm(10);

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
					printf("recvmsg receive %ld bytes from sockfd\n",n);
					parseRecv(n - sizeof(control));			//数据字节数,故要减去控制字段字节
				}
			}
		}
	}


	return 0;
}




void inform_server()
{
	char command[commandlen] = {0};
	int c;
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
	char addr[16] = {0};
	inet_ntop(AF_INET, &localaddr.sin_addr, addr, 16);
	printf("local port is %s:%hu\n",addr,ntohs(localaddr.sin_port));
	req2serv(command, sizeof(command), name, namelen);			//第四个参数取决于服务器接收端的第二个buf
}

//根据control字段决定发给谁，发什么
void operate(char* control, size_t len1, char* mes, size_t len2)
{
	if(strcmp(control,"update") == 0){
		req2serv(control, len1, mes, len2);
	}
	else if(strcmp(control,"quit") == 0){
		req2serv(control,len1,name,namelen);
		exit(5);
	}
	else if(strcmp(control,"chatall") == 0){
		chat2all(control, len1, mes, len2);
	}
	else if(strcmp(control,"chat") == 0){
		chat2one(control, len1, mes, len2);
	}
	else if(strcmp(control,"show") == 0){
		show_users();
	}
	else if(strcmp(control,"help") == 0){
		printf("command \"show\" will show you which users are online\n");
		printf("command \"chat somebody something\" will send something to a specific one\n");
		printf("command \"chatall something\" will send something to all online users\n");
		printf("command \"update\" will request server to update online user list\n");
	}
	else
		printf("unknown control message\n");


}

void chat2one(char* control, size_t len1, char* mes, size_t len2)
{
	char *pos = NULL;
	if((pos = strchr(mes,' ')) == NULL){
		printf("unknown control message\n");	
		return;
	}	
	int onlinenums = usermap.size();
	ssize_t n = 0;
	if(onlinenums == 0){
		printf("none is online\n");
		return;
	}
	char peername[12] = {0};
	memcpy(peername,mes,pos-mes);
	struct sockaddr_in peeraddr;
	struct sockaddr_in alteraddr;
	msgsend.msg_namelen = sizeof(peeraddr);	
	iovsend[0].iov_base = control;
	iovsend[0].iov_len = len1;
	iovsend[1].iov_base = name;
	iovsend[1].iov_len = namelen;
	iovsend[2].iov_base = pos+1;
	iovsend[2].iov_len = len2-(pos+1-mes);
	string sname(peername);
	map<string,struct user>::iterator it = usermap.find(sname);
	if(it == usermap.end()){
		printf("not such user\n");
		return;
	}
	peeraddr = it->second.addr;
	if(outeraddr.sin_addr.s_addr == peeraddr.sin_addr.s_addr){	//发送端和本机处于同一局域网
		alteraddr = it->second.inner_addr;
		msgsend.msg_name = (struct sockaddr*)&alteraddr;
	}
	else{
		msgsend.msg_name = (struct sockaddr*)&peeraddr;	
	}
	if( (n = sendmsg(sockfd, &msgsend, 0)) < 0)
		err_sys("sendmsg to server error");
	return;
}



void chat2all(char* control, size_t len1, char* mes, size_t len2)
{	
	int onlinenums = usermap.size();
	ssize_t n = 0;
	if(onlinenums == 0){
		printf("none is online\n");
		return;
	}
	struct sockaddr_in peeraddr;
	struct sockaddr_in alteraddr;
	msgsend.msg_namelen = sizeof(peeraddr);	
	iovsend[0].iov_base = control;
	iovsend[0].iov_len = len1;
	iovsend[1].iov_base = name;
	iovsend[1].iov_len = namelen;
	iovsend[2].iov_base = mes;
	iovsend[2].iov_len = len2;
	
	map<string,struct user>::iterator it;
	for(it = usermap.begin(); it != usermap.end(); ++it){	
		peeraddr = it->second.addr;
		char addrstr[16] = {0};
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
		if( (n = sendmsg(sockfd, &msgsend, 0)) < 0)
			err_sys("sendmsg to server error");	
	}	
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
	for(it = usermap.begin(); it!=usermap.end(); ++it){
		printf("%s, ",it->second.name);
	}
	printf("\n");

}



void req2serv(char* control, size_t len1, char* mes, size_t len2)
{
	msgsend.msg_name = (struct sockaddr*)&servaddr;
	msgsend.msg_namelen = sizeof(servaddr);
	ssize_t n = 0;	
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
	
void parseRecv(ssize_t n)
{
	if(strcmp((char*)iovrecv[0].iov_base,"login") == 0 || strcmp((char*)iovrecv[0].iov_base,"update") == 0){
		update_map(n);
	}
	else if(strcmp((char*)iovrecv[0].iov_base,"chatall") == 0 || strcmp((char*)iovrecv[0].iov_base,"chat") == 0){	//iovrecv[1]的前12个字节是发送端的名字
		char fromwho[namelen];
		memcpy(fromwho,iovrecv[1].iov_base,12);
		printf("%s:%s\n",fromwho,(char*)(iovrecv[1].iov_base)+12);
	}
	else if(strcmp((char*)iovrecv[0].iov_base,"try") == 0){
		printf("unexpected try control\n");		
		//control字段为try表示udp打洞消息不过应该永远都收不到这个消息才对
	}
	else if(strcmp((char*)iovrecv[0].iov_base,"delete") == 0)
		delete_user((char*)iovrecv[1].iov_base);
	else if(strcmp((char*)iovrecv[0].iov_base,"add") == 0)
		add_user();	
	else
		printf("unknown receive\n");
}

void update_map(ssize_t n)
{
	unsigned long int num = n/sizeof(struct user);
	printf("n is %lu and the num of user is %lu\n",n,num);
	struct user tempuser;
	unsigned int i;
	char control[commandlen] = "try";
	std::pair< map<string, struct user>::iterator,bool> ret;	//用来判断是否插入成功，如果插入成功，说明是新用户，则需要发送udp打洞探测包
	for(i = 0; i < num; i++){
		memcpy(&tempuser, (char*)iovrecv[1].iov_base+i*sizeof(struct user), sizeof(struct user));
		string stemp(tempuser.name);
		if(stemp != string_name){				//需要加入自己，用以比较目标和自己是否处于同一个局域网
			ret = usermap.insert(map<string, struct user>::value_type(stemp,tempuser));			//收到的地址都是网络字节序
			if(ret.second == true){
				sendto(sockfd, control, sizeof(control), 0, (struct sockaddr*)&tempuser.addr, sizeof(tempuser.addr));
			}
			else
				printf("already exist\n");		
		}
		else if(outeraddr.sin_port == 0) {		//结构体尚未赋值
			printf("outeraddr\n");
			outeraddr = tempuser.addr;
		}
	}
}

void add_user()
{
	struct user tempuser;
	char control[commandlen] = "try";
	memcpy(&tempuser, (char*)iovrecv[1].iov_base, sizeof(struct user));
	string stemp(tempuser.name);
	if(usermap.find(stemp)!=usermap.end())
		usermap.erase(stemp);
	usermap.insert(map<string, struct user>::value_type(stemp,tempuser));
	sendto(sockfd, control, sizeof(control), 0, (struct sockaddr*)&tempuser.addr, sizeof(tempuser.addr));
	printf("%s is online\n",tempuser.name);
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

void delete_user(char* target_name)
{
	string sname(target_name);
	if(usermap.erase(sname) != 0)
		printf("delete %s successfully\n",target_name);
	return;
}

void alarm_handler(int signo)
{
	printf("detect sigalarm\n");
	int save_errno = errno;
	errno = save_errno;

}

void interrupt_handler(int signo)
{
	printf("detect sigint\n");
	exit(10);

}
