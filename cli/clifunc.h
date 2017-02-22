#pragma once
extern "C"{
#include "../common.h"
}
#include <map>
#include <string>
#include <iostream>
using std::string;
using std::map;
void getlocalip();
void update_map(ssize_t);
int parseInput(char*, char*, size_t, char*);		//参数分别是键盘输入，存放解析后命令的buf，buf的大小
void parseRecv(ssize_t, char*, char*);
void inform_server();
void operate(char*, size_t, char*, size_t);
void req2serv(char*, size_t, char*, size_t);
void chat2all(char*, size_t, char*, size_t);
void chat2one(char*, size_t, char*, size_t);
void file_request(char*, size_t, char*, size_t);
void delete_user(char*);
void add_user();
void show_users();
void alarm_handler(int);
void interrupt_handler(int);
void *thread_heart(void *);
void *thread_listen(void *);
void *thread_connect(void *);
void *thread_tcp1(void *);
void *thread_tcp2(void *);
void *thread_tcp3(void *);
void *thread_tcp4(void *);
void listen_cleanup(void *);
void connect_cleanup(void *);
void sendfile(int, struct file *, int);
void recvfile(int);


extern int sockfd;
extern struct msghdr msgsend, msgrecv;		
extern struct iovec iovsend[3], iovrecv[2];
extern char name[namelen+2];				
extern string string_name;
extern pthread_mutex_t maplock;
extern struct sockaddr_in servaddr;
extern struct sockaddr_in localaddr;		
extern struct sockaddr_in outeraddr;		
extern struct epoll_event ev,events[cliMAX_EVENTS];
extern map<string,struct user> usermap;
