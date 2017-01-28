objs = servfunc.o udpserv.o
CC = g++ -Wall -std=c++11
CFLAGS = -lpthread
serv: $(objs)
	$(CC) $(objs) -o serv $(CFLAGS)
servfunc.o: servfunc.cpp servfunc.h ../common.h
	$(CC) -c servfunc.cpp
udpserv.o: udpserv.cpp servfunc.h ../common.h
	$(CC) -c udpserv.cpp
clean:
	-rm $(objs)
.PHONY: clean
