#pragma once

extern "C"{
#include "../common.h"
}
#include <queue>
#include <string>
#include <iostream>
#include <map>
using std::queue;
using std::string;
using std::map;
using std::cout;
using std::endl;


struct job{
	int fd;				//完成该任务的套接字
	time_t reachtime;	//暂未用到
	char control[12];
	char peer_name[12];
	struct sockaddr_in peeraddr;
	struct sockaddr_in peerlocaladdr;
};

void save_cli(const struct job*);
void do_job(struct job*);
int parse_command(const char *);			//解析命令
void send_usermap(const int, char*);				//发送在线用户
void* thread_main(void*);

extern pthread_mutex_t joblock;
extern pthread_mutex_t maplock;
extern pthread_cond_t condready;
extern queue<struct job*> jobs;			//任务队列
extern map<string,struct user> usermap;	//保存当前登陆用户,string为用户名

