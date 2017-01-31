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
	return -1;
}

void send_usermap(const int fd, char* control)
{
	unsigned int num;
	if(pthread_mutex_lock(&maplock) != 0)
		err_sys("map lock failed");
	num = usermap.size();
	//printf("usermap size is %u\n",num);
	//uint32_t temp = htonl(num);
	
	struct msghdr msgsend;
	bzero(&msgsend, sizeof(msgsend));
	struct iovec* iovsend = (struct iovec*)malloc((num+1) * sizeof(struct iovec));
	bzero(iovsend, (num+1) * sizeof(struct iovec));
	msgsend.msg_iov = iovsend;
	msgsend.msg_iovlen = num + 1;
	iovsend[0].iov_base = control;
	iovsend[0].iov_len = sizeof(((struct job*)0)->control);
	
	int i = 1;
	printf("map size is %u\n",num);
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
	
}


void save_cli(const struct job* currentjob)
{
	string cliname(currentjob->peer_name);
	struct user current_user;
	strcpy(current_user.name,currentjob->peer_name);
	current_user.addr = currentjob->peeraddr;
	current_user.inner_addr = currentjob->peerlocaladdr;
	if(pthread_mutex_lock(&maplock) != 0)
		err_sys("map lock failed");
	usermap.insert(map<string, struct user>::value_type(cliname,current_user));
	printf("成功插入\n");
	if(pthread_mutex_unlock(&maplock) != 0)
		err_sys("map unlock failed");
}

