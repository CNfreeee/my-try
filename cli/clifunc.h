#pragma once
extern "C"{
#include "../common.h"
}

void getlocalip();
void update_map(ssize_t);
int parseInput(char *, char *, size_t, char*);		//参数分别是键盘输入，存放解析后命令的buf，buf的大小
void parseRecv(ssize_t);
void inform_server();
void operate(char*, size_t, char*, size_t);
void req2serv(char*, size_t, char*, size_t);
void chat2all(char*, size_t, char*, size_t);
void chat2one(char*, size_t, char*, size_t);
void delete_user(char*);
void add_user();
void show_users();
void alarm_handler(int);
void interrupt_handler(int);
void* thread_heart(void*);
