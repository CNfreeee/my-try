#pragma once

extern "C"{
#include "../common.h"
}
#include <deque>
#include <string>
#include <iostream>
#include <map>
using std::deque;
using std::string;
using std::map;
using std::cout;
using std::endl;


struct job{
	int fd;				//完成该任务的套接字
	time_t reachtime;	//暂未用到
	char control[commandlen];
	char peer_name[namelen];
	struct sockaddr_in peeraddr;
	struct sockaddr_in peerlocaladdr;
};


struct filepair{
	int sendpeer;
	int recvpeer;
};

void save_cli(const struct job *);
void do_job(struct job *);
int parse_command(const char *);			//解析命令
void send_usermap(const int, char *);				//发送在线用户
void delete_user(const int, char *);			//用户下线，删除该用户并告知其他人
void inform_others(struct user *);			//用户上线，告知其他人
void alarm_handler(int);
void *thread_main(void *);
void *thread_detect(void *);
void *thread_tcp(void *);
extern pthread_mutex_t joblock;
extern pthread_mutex_t maplock;
extern pthread_cond_t condready;
extern deque<struct job *> jobs;			//任务队列
extern map<string,struct user> usermap;	//保存当前登陆用户,string为用户名
extern int epollfd;

