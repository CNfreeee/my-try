objs = udpcli.o clifunc.o
CC = g++ -Wall -std=c++11
cli: $(objs)
	$(CC) $(objs) -o cli
udpcli.o: udpcli.cpp
	$(CC) -c udpcli.cpp
clifunc.o: clifunc.cpp
	$(CC) -c clifunc.cpp
clean:
	-rm $(objs)
.PHONY: clean
