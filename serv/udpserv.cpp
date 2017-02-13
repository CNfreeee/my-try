#include "servfunc.h"

int epollfd;

static struct epoll_event ev,events[servMAX_EVENTS];

deque<struct job*> jobs;
map<string,struct user> usermap;

pthread_mutex_t joblock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t maplock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condready = PTHREAD_COND_INITIALIZER;
					


int main(int argc, char **argv)
{
	int sockfd;
	long int i;
	struct sockaddr_in servaddr,cliaddr;
	struct msghdr msgrecv;
	char command[commandlen];
	char recvname[namelen];
	struct sockaddr_in peerlocaladdr;
	struct iovec iovrecv[3];
	
	/*struct sigaction SIGALRM_act;
	SIGALRM_act.sa_handler = alarm_handler;
	sigemptyset(&SIGALRM_act.sa_mask);
	SIGALRM_act.sa_flags = 0;
	SIGALRM_act.sa_flags |= SA_RESTART;
	if(sigaction(SIGALRM, &SIGALRM_act, NULL) < 0)
		err_sys("sigaction error\n");
	*/
	bzero(&msgrecv, sizeof(msgrecv));
	bzero(&peerlocaladdr,sizeof(peerlocaladdr));
	msgrecv.msg_name = (struct sockaddr *)&cliaddr;
	socklen_t len = sizeof(cliaddr);
	msgrecv.msg_namelen = len;
	msgrecv.msg_iov = iovrecv;
	msgrecv.msg_iovlen = 3;
	iovrecv[0].iov_base = command;
	iovrecv[0].iov_len = sizeof(command);
	iovrecv[1].iov_base = recvname;
	iovrecv[1].iov_len = sizeof(recvname);
	iovrecv[2].iov_base = &peerlocaladdr;
	iovrecv[2].iov_len = sizeof(peerlocaladdr);

				//ipv4地址大小
	if( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		err_sys("socket error");
	int defaultbuf;
	socklen_t bufsize = sizeof(socklen_t);
	getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &defaultbuf, &bufsize);
	printf("udp默认接收缓冲区的大小是%d\n",defaultbuf);

	bzero(&servaddr, sizeof(servaddr));
	
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port        = htons(PORT-1);
	
	const int on = 1;
	if(setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on)) < 0)	//设置重复绑定选项
		err_sys("setsockopt error");
	if(setsockopt(sockfd,SOL_SOCKET,SO_REUSEPORT,&on,sizeof(on)) < 0)	
		err_sys("setsockopt error");


	if(bind(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
		err_sys("bind error");

	if( (epollfd = epoll_create(servMAX_EVENTS)) == -1)			//注册事件
		err_sys("epoll_create error");
	ev.events = EPOLLIN;
	ev.data.fd = sockfd;
	if(epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &ev) == -1)
		err_sys("epoll_ctl error");
	
	pthread_t *pth = (pthread_t*)malloc((threadsnum+1) * sizeof(pthread_t));
	for(i = 0; i < threadsnum; ++i){									//创建线程池
		if(pthread_create(&pth[i], NULL, &thread_main, NULL)!= 0)
			err_sys("pthread poll create failed");
	}
	if(pthread_create(&pth[i], NULL, &thread_detect, NULL)!= 0)
		err_sys("pthread poll create failed");
	int nfds,m;
	char addrstr[16] = {0};
	ssize_t n;
	int newfd;
	int tempsockfd;
	for(;;){
		if( (nfds = epoll_wait(epollfd, events, cliMAX_EVENTS, -1)) == -1){				//考虑被信号中断的情况
			if(errno == EINTR)
				continue;
			err_sys("epoll_wait error");
		}
		for(m = 0; m < nfds; ++m){
			if(events[m].data.fd == sockfd){
				if(events[m].events & EPOLLIN){
					//bzero(command,sizeof(command));				//收到的数据不以'\0'结尾，需手动加上
					bzero(recvname,sizeof(recvname));
					if( (n = recvmsg(sockfd, &msgrecv, 0)) < 0){
						if(errno == ECONNREFUSED)
							continue;
																		
						else
							err_sys("recvfrom error");
					}
					printf("receive %lu bytes\n",n);
					printf("receive control message %s from %s\n",(char*)iovrecv[0].iov_base, (char*)iovrecv[1].iov_base);
					if(strcmp(command,"try connect") == 0){			
						continue;
					}
					if(inet_ntop(AF_INET, &(cliaddr.sin_addr),addrstr,sizeof(addrstr)) == NULL)	//获取客户端的地址
						err_sys("inet_ntop error");
					printf("receive message from %s: %hu \n",addrstr, ntohs(cliaddr.sin_port));	//网络字节序转成主机字节序显示
				
					if( (newfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
						err_sys("socket error");
					if(setsockopt(newfd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on)) < 0)	//设置重复绑定选项
						err_sys("setsockopt error");
					if(setsockopt(newfd,SOL_SOCKET,SO_REUSEPORT,&on,sizeof(on)) < 0)	
						err_sys("setsockopt error");
					if(bind(newfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
						err_sys("bind error");
					if( connect(newfd, (struct sockaddr *) &cliaddr, sizeof(cliaddr)) < 0)		
							err_sys("connect error");
					
					struct job* newjob = (struct job*)malloc(sizeof(struct job));		//将此请求加入任务队列					
					bzero(newjob,sizeof(struct job));
					strcpy(newjob->control,(char*)iovrecv[0].iov_base);
					newjob->fd = newfd;
					strcpy(newjob->peer_name,(char*) iovrecv[1].iov_base);
					newjob->peeraddr = cliaddr;					//保存的是网络字节序地址	
					newjob->peerlocaladdr = peerlocaladdr;
					if(pthread_mutex_lock(&joblock) != 0)
						err_sys("job lock failed\n");
					jobs.push_back(newjob);
					if(pthread_mutex_unlock(&joblock) != 0)
						err_sys("job unlock failed\n");
					if(pthread_cond_signal(&condready) != 0)
						err_sys("cond signal failed\n");

					ev.data.fd = newfd;
					if(epoll_ctl(epollfd, EPOLL_CTL_ADD, newfd, &ev) == -1)
						err_sys("epoll_ctl error");
					
				}
			}
			else{
					tempsockfd = events[m].data.fd;
					//bzero(command,sizeof(command));		//发送端在发command前会将缓冲区清空，所以注释掉这句
					if( (n = recvmsg(tempsockfd, &msgrecv, 0)) < 0){							
						if(errno == ECONNREFUSED)
							continue;						
						else
							err_sys("recvfrom error");
					}
					
					if(strcmp(command, "heart") == 0){		//收到心跳包
						string sname((char*)iovrecv[1].iov_base);
						++usermap.find(sname)->second.count;
						continue;
					}

					if(inet_ntop(AF_INET, &cliaddr.sin_addr,addrstr,sizeof(addrstr)) == NULL)	//获取客户端的地址
						err_sys("inet_ntop error");
					printf("receive message from %s: %hu \n",addrstr, ntohs(cliaddr.sin_port));	//网络字节序要转成主机字节
					printf("sockid is %d\n",tempsockfd);
					printf("receive control message is **%s**\n", (char*)iovrecv[0].iov_base);
					
					struct job* newjob = (struct job*)malloc(sizeof(struct job));	
					bzero(newjob,sizeof(struct job));
					strcpy(newjob->control,(char*)iovrecv[0].iov_base);
					newjob->fd = tempsockfd;
					pthread_mutex_lock(&joblock);
					//要根据发送的用户名来更新在线用户列表
					if(strcmp(command,"quit") == 0){			
						strcpy(newjob->peer_name,(char*) iovrecv[1].iov_base);
						jobs.push_front(newjob);			//此消息比较重要，放在工作队列队首
					}
					else
						jobs.push_back(newjob);
					pthread_mutex_unlock(&joblock);
					pthread_cond_signal(&condready);		

			}
		}
		
	}

	
}



void* thread_main(void* arg)
{
	struct job* currentjob;
	for(; ;){
		if(pthread_mutex_lock(&joblock) != 0)
			err_sys("job lock failed\n");
		while(jobs.empty())
			pthread_cond_wait(&condready,&joblock);
		currentjob = jobs.front();
		jobs.pop_front();
		if(pthread_mutex_unlock(&joblock) != 0)
			err_sys("job unlock failed\n");
		do_job(currentjob);
		free(currentjob);
	}
	return (void*)0;
}

void* thread_detect(void* arg)
{
	map<string,struct user>::iterator it;
	char control[commandlen] = "quit";
	sleep(10);
	for(;;){
		pthread_mutex_lock(&maplock);
		if(usermap.size() == 0)			
			goto skip;
		
		for(it = usermap.begin(); it != usermap.end(); ++it){
			if(it->second.count > 0)
				it->second.count = 0;
			else{
				struct job* newjob = (struct job*)malloc(sizeof(struct job));						
				bzero(newjob,sizeof(struct job));
				strcpy(newjob->control,control);
				newjob->fd = it->second.bind_fd;
				strcpy(newjob->peer_name,it->second.name);
				pthread_mutex_lock(&joblock);
				jobs.push_front(newjob);
				pthread_mutex_unlock(&joblock);
				pthread_cond_signal(&condready);
			}		
		}
	skip:	
		pthread_mutex_unlock(&maplock);
		sleep(10);
	}
	return (void*)0;
}

void alarm_handler(int signo)
{
	int save_errno = errno;
	sleep(50);
	errno = save_errno;

}





	
