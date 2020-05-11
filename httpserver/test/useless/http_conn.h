#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include<unistd.h>
#include<signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include "../lock/lock.h"

class htt_conn
{
public:
	static const int FILENAME_LEN = 200;
	static const int READ_BUFFER_SIZE = 2048;
	static const int WRITE_BUFFERSIZE = 10240;

//对于每个连接都有如下成员
private:
	int m_sockfd;
	struct sockaddr_in m_addr;
	
	//读缓冲区
	char m_read_buf[READ_BUFFER_SIZE];
	int m_read_idx;//下次将数据读到m_read_idx位置

	//写缓冲区
	char m_write_buf[WRITE_BUFFER_SIZE];
	int m_write_idx;//下次数据从m_write_idx位置开始写到客户端
	//int m_bytes_to_send;

//全部的连接共享
public:
	static int m_epollfd;
	static int m_user_count;

piblic:

	//http_conn();
	//~http_conn();

	void init(int sockfd,const sockaddr_in &addr);
	void init();
	void process();
	bool m_read();
	bool m_write();
};

void http_conn::init(int sockfd,const sockaddr_in &addr)
{
	m_sockfd = sockfd;
	m_address = addr;
	addfd(m_epollfd,sockfd,true);
	m_user_count++;
	init();
}
//init buffer
void http_conn::init()
{
	memset(m_read_buf,'\0',READ_BUFFER_SIZE);
	memset(m_write_buf,'\0',WRITE_BUFFERSIZE);
	m_read_idx = 0;
	m_write_idx = 0;
}
void http::conn::m_read()
{
	if(m_read_idx >= READ_BUFFER_SIZE)
		return false;

	int bytes_read = 0;
	while(1)
	{
		bytes_read = recv(m_sockfd,m_read_buf + m_read_idx,
						  READ_BUFFER_SIZE - m_read_idx ,0 );
		if(bytes_read <= 0)
		{
			if(errno==EAGAIN||errno == EWOULDBLOCK)
				return true;
			return false;
		}
		m_read_idx += bytes_read;
	}
	return true;
}

bool http_conn::m_write()
{
	int bytes_write = 0;
	int tol_bytes_to_send = strlen(m_write_buf);
	int left = tol_bytes_to_send - m_write_idx;
	while(1)
	{
		if(left <= 0)
		{
			init();
			modfd(m_epollfd,_m_sockfd,EPOLLIN);
			return true;
		}

		bytes_write = write(fd,m_write_idx+m_write_idx,left);
		if(bytes_write <= 0)
		{
			if(errno == EAGAIN || errno == EWOULDBLOCK)
			{
				modfd(m_epollfd,m_sockfd,EPOLLOUT);
				return true;
			}
			else
				return false;
		}
		left -= bytes_write;
	}
}

void http_conn::process()
{
	m_write_idx = 0;
	for(int i = 0;i < 99;i++)
		m_write_buf[i] = 's';
	m_write_buf[99] = '\0';
	modfd(m_epollfd, m_sockfd, EPOLLOUT);
	m_write();
}

#endif
