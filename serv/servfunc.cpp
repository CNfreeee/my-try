#include "servfunc.h"


void err_sys(const char *str)
{
	perror(str);
	exit(1);
}


void do_job(struct job* currentjob)
{
	pthread_t threid;
	threid = pthread_self();
	printf("current pthread id is %lu\n",threid);
	printf("current job's control message is %s, connect fd is %d\n",currentjob->control,currentjob->fd);
	int handle;
	handle = parse_command(currentjob->control);
	switch(handle){
		case 1:
			save_cli(currentjob);
			send_usermap(currentjob->fd, currentjob->control);
			break;
		case 2:
			send_usermap(currentjob->fd, currentjob->control);
			break;
		case 3:
			delete_user(currentjob->fd, currentjob->peer_name);
			break;
		default:
			printf("unknown command\n");
			break;
	}
	return;
}

int parse_command(const char* control)
{
	if(strcmp(control,"login") == 0){
		printf("receive %s\n", control);
		return 1;
	}
	if(strcmp(control,"update") == 0){
		printf("receive %s\n", control);
		return 2;
	}
	if(strcmp(control,"quit") == 0){
		printf("receive %s\n",control);
		return 3;
	}
	return -1;
}

void send_usermap(const int fd, char* control)
{
	unsigned int num;
	
	num = usermap.size();	
	struct msghdr msgsend;
	bzero(&msgsend, sizeof(msgsend));
	if(pthread_mutex_lock(&malloclock) != 0)
		err_sys("malloc lock failed\n");
	struct iovec* iovsend = (struct iovec*)malloc((num+1) * sizeof(struct iovec));
	if(pthread_mutex_unlock(&malloclock) != 0)
		err_sys("malloc unlock failed\n");
	bzero(iovsend, (num+1) * sizeof(struct iovec));
	msgsend.msg_iov = iovsend;
	msgsend.msg_iovlen = num + 1;
	iovsend[0].iov_base = control;
	iovsend[0].iov_len = sizeof(((struct job*)0)->control);
	
	int i = 1;
	printf("map size is %u\n",num);
	if(pthread_mutex_lock(&maplock) != 0)
		err_sys("map lock failed");
	if( num != 0){
		map<string,struct user>::iterator it;
		for(it = usermap.begin(); it != usermap.end(); ++it){
			iovsend[i].iov_base = &it->second;
			iovsend[i].iov_len = sizeof(struct user);
			++i;		
		}
	}
	if(sendmsg(fd, &msgsend, 0) < 0)
		err_sys("sendmsg error\n");
	printf("******************\n");
	free(iovsend);	
	if(pthread_mutex_unlock(&maplock) != 0)
		err_sys("map unlock failed");
	return;
	
}


void save_cli(const struct job* currentjob)
{
	string cliname(currentjob->peer_name);
	struct user current_user;
	strcpy(current_user.name,currentjob->peer_name);
	current_user.addr = currentjob->peeraddr;
	current_user.inner_addr = currentjob->peerlocaladdr;
	current_user.bind_fd = currentjob->fd;
	
	inform_others(&current_user);		//告知在线用户新用户上线
	
	if(pthread_mutex_lock(&maplock) != 0)
		err_sys("map lock failed");
	if(usermap.find(cliname)!=usermap.end())
		usermap.erase(cliname);
	usermap.insert(map<string, struct user>::value_type(cliname,current_user));
	if(pthread_mutex_unlock(&maplock) != 0)
		err_sys("map unlock failed");
	return;
}


void inform_others(struct user* new_user)
{
	struct msghdr msgsend;
	struct iovec iovsend[2];
	map<string, struct user>::iterator it;
	char control[commandlen] = "add";
	bzero(&msgsend, sizeof(msgsend));
	msgsend.msg_iov = iovsend;
	bzero(iovsend,2*sizeof(struct iovec));
	msgsend.msg_iovlen = 2;
	iovsend[0].iov_base = control;
	iovsend[0].iov_len = sizeof(control);
	iovsend[1].iov_base = new_user;
	iovsend[1].iov_len = sizeof(struct user);
	if(pthread_mutex_lock(&maplock) != 0)
		err_sys("map lock failed");
	if(usermap.size() == 0){
		if(pthread_mutex_unlock(&maplock) != 0)
			err_sys("map unlock failed");
		return;
	}
	for(it = usermap.begin(); it != usermap.end(); ++it){
		if(sendmsg(it->second.bind_fd, &msgsend, 0) < 0){		
			if(errno == ECONNREFUSED)
				continue;
															
			else
				err_sys("recvfrom error");
		}

	} 
	if(pthread_mutex_unlock(&maplock) != 0)
		err_sys("map unlock failed");
	return;
}


void delete_user(const int fd, char* name)
{
	struct msghdr msgsend;
	struct iovec iovsend[2];
	map<string, struct user>::iterator it;
	char control[commandlen] = "delete";	
	string sname(name);
	bzero(&msgsend, sizeof(msgsend));
	msgsend.msg_iov = iovsend;
	bzero(iovsend,2*sizeof(struct iovec));
	msgsend.msg_iovlen = 2;
	iovsend[0].iov_base = control;
	iovsend[0].iov_len = sizeof(control);
	iovsend[1].iov_base = name;
	iovsend[1].iov_len = namelen;
	if(pthread_mutex_lock(&maplock) != 0)
		err_sys("map lock failed");
	if(usermap.erase(sname) != 1){
		if(pthread_mutex_unlock(&maplock) != 0)
			err_sys("map unlock failed");
		return;
	}
	if(usermap.size() == 0){
		if(pthread_mutex_unlock(&maplock) != 0)
			err_sys("map unlock failed");
		return;
	}
	
	for(it = usermap.begin(); it != usermap.end(); ++it){
		if(sendmsg(it->second.bind_fd, &msgsend, 0) < 0){		
			if(errno == ECONNREFUSED)
				continue;
															
			else
				err_sys("recvfrom error");
		}

	} 
	if(pthread_mutex_unlock(&maplock) != 0)
		err_sys("map unlock failed");
	if(-1 == epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL))
		err_sys("epoll_ctl error\n");
	close(fd);
	printf("delete %s\n",name);
	return;
}
