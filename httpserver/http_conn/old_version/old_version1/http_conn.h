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
#include "../pub/pub.h"

class http_conn
{
//全部的连接共享
public:
	static int m_epollfd;
	static int m_user_count;
public:
	//http_conn();
	//~http_conn();
	void init(int sockfd,const sockaddr_in &addr);
	void init();
	bool m_read();
	bool m_write();


//读写缓冲区
public:
	static const int READ_BUFFER_SIZE = 2048;
	static const int WRITE_BUFFER_SIZE=1024;//响应状态行,响应头
	static const int FILENAME_LEN = 200;
private:
	int m_sockfd;
	struct sockaddr_in m_addr;
	//读缓冲区
	char m_read_buf[READ_BUFFER_SIZE];
	int m_read_idx;//下次将数据读到m_read_idx位置
	//写缓冲区
	char m_write_buf[WRITE_BUFFER_SIZE];
	int m_write_idx;//下次数据从m_write_idx位置开始写到客户端
	int m_bytes_to_send;
	int m_bytes_havesend;

//http状态机
public:
	enum METHOD
	{GET = 0,POST};

	//主状态机的三种状态：正在分析请求行，正在分析请求头部，正
	//在分析请求内容
	enum CHECK_STATE{CHECK_STATE_REQUESTLINE = 0,
		CHECK_STATE_HEADER,CHECK_STATE_CONTENT};

	//从状态机：读取到一个完整的行，行出错，行数据尚未完整
	enum LINE_STATUS{LINE_OK = 0,LINE_BAD,LINE_OPEN};

	//服务器处理HTTP请求过程中可能产生的状态：
	//NO_REQUEST:请求数据不完整,需要继续获取请求数据；
	//GET_REQUEST：获得了一个完整的请求；
	//BAD_REQUEST：用户请求语法错误；
	//NO_RESOURCE: 请求的URL文件名不存在
	//FORBIDDEN_REQUEST：对资源没有访问权限
	//FILE_REQUEST: 表示这是文件请求
	//INTERNAL_ERROR:服务器内部错误
	//CLOSED_CONNECTION:客户已经关闭连接了
	enum HTTP_CODE
	{NO_REQUEST,GET_REQUEST,BAD_REQUEST,NO_RESOURCE,
	FORBIDDEN_REQUEST,FILE_RESOURCE,
	INTERNAL_ERROR,CLOSE_CONNECTION};

private:
	//解析请求报文
	int m_checked_idx;
	int m_start_line;
	CHECK_STATE m_check_state;
	METHOD m_method;
	char *m_version;
	char *m_host;
	int m_content_length;		   //请求报文中的content-length
	bool m_linger;				   //keep-alive
	
	//响应报文
	char m_real_file[FILENAME_LEN];//目标文件绝对路径名
	char *m_url;				   //请求报文中的目标文件url
	char *m_file_address;	   //目标文件被mmap到内存中的地址
	struct stat m_file_state;  //目标文件的状态
   	//m_iv[0]:响应状态行，响应头
	//m_iv[1]:响应内容（即目标文件）
	struct iovec m_iv[2];
	int m_iv_count;			  //请求目标文件时为2，否则为1

public:
	//分析HTTP请求
	HTTP_CODE parse_requestline(char* text);
	HTTP_CODE parse_headers(char*text);
	HTTP_CODE parse_content(char*text);
	HTTP_CODE do_request();
	LINE_STATUS parse_line();

	HTTP_CODE process_read();
	HTTP_CODE process_write(HTTP_CODE ret);
	void process();
};
#endif
