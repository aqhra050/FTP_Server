all: client server

client: client.c common.o
	gcc  -Wall client.c common.o -o client -pthread

server: server.c common.o
	gcc -Wall server.c common.o -o server -pthread

common: common.c
	gcc -Wall -c common.c common.o

clean:
	rm server client common.o
