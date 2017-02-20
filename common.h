#pragma once
#define	MAXLINE		4096	/* max text line length */
#define	BUFFSIZE	8192	/* buffer size for reads and writes */
#define	LISTENQ		1024	/* 2nd argument to listen() */
#define UDPMAXLINE      548
//#define UDPMAXLINE      4096	//先设一个较大值，之后再考虑udp数据报分片的问题
#define cliMAX_EVENTS	10
#define servMAX_EVENTS  1000
#define threadsnum 3
#define namelen 12
#define commandlen 12
#define PORT 9877
#include <stdlib.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/epoll.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <sys/stat.h>

//发送文件请求时交换的file结构体
struct file{
	int fd;				//打开的要被发送文件的文件描述符
	off_t filesize;	
	pthread_t thread_id;		//处理的线程
	char fileowner[namelen];
	struct sockaddr_in addr;	//消息发送方的地址，每次发送都要修改
	char fileaddr[64];
};

struct file_arg{
	int flag;		//1代表文件发送方，2代表文件接收方
	int listenfd;
	int fd;			//同file中的fd
	in_port_t port;		//tcp打洞中用
	//char name[namelen];
	struct sockaddr_in peeraddr;	//tcp地址
	struct file myfile;
};

//客户结构体，包含客户的地址，名字，最后活跃时间
struct user{
	int bind_fd;				//与该用户绑定的套接字
	int count;
	char name[namelen];			//用户名
	struct sockaddr_in addr;		//客户端地址
	struct sockaddr_in inner_addr;		//对局域网主机来说的本地地址
};


	
void err_sys(const char*);
