#include <stdio.h>
#include <stdlib.h>
#include <string.h>						
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define MAXLINE 4096

void err_sys(const char*fmt)
{
	perror(fmt);
	exit(1);
}
void err_quit(const char*fmt)
{
    perror(fmt);
    exit(1);
}

int main(int argc, char **argv)
{
	int sockfd,n;
	char recvline[MAXLINE+1];
	struct sockaddr_in servaddr;
	
	if(argc!=3)
		err_quit("arg error");
	if((sockfd = socket(AF_INET, SOCK_STREAM, 0))<0)
		err_sys("socket error");

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(atoi(argv[2]));
	if(inet_pton(AF_INET,argv[1],&servaddr.sin_addr)<=0)
		err_quit("inet_pton err");

	if(connect(sockfd,(struct sockaddr*)&servaddr,sizeof(servaddr))<0)
		err_sys("connect err");
	
	char buf[100];
	for(int i = 0;i < 99;i++)
		buf[i] = 'r';
	buf[99] = '\0';
	write(sockfd,buf,100);
	int count = 0;
	sleep(20);
	while(read(sockfd,buf,100)>0)
	{
		count++;
		if(count > 20&&count < 22)
			printf("%s\n",buf);
	}
	printf("%d\n",count);
	exit(0);
}
