all : client server

client : client.o
	gcc -o client client.o

server : server.o
	g++ -o server server.o

server.o : main.c pub.cpp
	g++ -c main.c pub.cpp

client.o : client.c
	cc -c client.c

clean:
	rm server client
