#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#define MAX_FD 65535			//最大文件描述符(连接数+1)
#define MAX_EVENT_NUMBER 10000	//最大事件数
static int epollfd = 0;
void setnonblocking(int sock);
void addfd(int epollfd,int fd,bool one_shot);
void modfd(int epollfd,int fd,int ev);

static const int WRITE_BUFFER_SIZE = 4096000;
char m_write_buf[WRITE_BUFFER_SIZE];

class http_conn
{
public:
	static const int FILENAME_LEN = 200;
	static const int READ_BUFFER_SIZE = 2048;

//对于每个连接都有如下成员
private:
	int m_sockfd;
	struct sockaddr_in m_address;
	
	//读缓冲区
	char m_read_buf[READ_BUFFER_SIZE];
	int m_read_idx;//下次将数据读到m_read_idx位置

	//写缓冲区
	int m_write_idx;//下次数据从m_write_idx位置开始写到客户端
	//int m_bytes_to_send;

//全部的连接共享
public:
	static int m_epollfd;
	static int m_user_count;

public:

	//http_conn();
	//~http_conn();

	void init(int sockfd,const sockaddr_in &addr);
	void init();
	void process();
	bool m_read();
	bool m_write();
};
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;
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
	memset(m_write_buf,'\0',WRITE_BUFFER_SIZE);
	m_read_idx = 0;
	m_write_idx = 0;
}
bool http_conn::m_read()
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
	setnonblocking(m_sockfd);
	int bytes_write = 0;
	int tol_bytes_to_send = strlen(m_write_buf);
	int left = tol_bytes_to_send - m_write_idx;
	while(1)
	{
		if(left <= 0)
		{
			init();
			modfd(m_epollfd,m_sockfd,EPOLLIN);
			return true;
		}
		bytes_write = write(m_sockfd,m_write_buf+m_write_idx,left);
		printf("bytes write %d\n" , bytes_write);
		if(bytes_write <= 0)
		{
			if(errno == EAGAIN || errno == EWOULDBLOCK)
			{
				printf("EAGAIN\n");
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
	for(int i = 0;i < WRITE_BUFFER_SIZE-1;i++)
		m_write_buf[i] = 's';
	m_write_buf[WRITE_BUFFER_SIZE-1] = '\0';
	modfd(m_epollfd, m_sockfd, EPOLLOUT);
	m_write();
}

//对文件描述符设置非阻塞
void setnonblocking(int sock)
{
     int opts;
     opts=fcntl(sock,F_GETFL,0);
     if(opts<0)
     {
          perror("fcntl(sock,GETFL)");
          exit(1);
     }
     opts = opts|O_NONBLOCK;
     if(fcntl(sock,F_SETFL,opts)<0)
     {
          perror("fcntl(sock,SETFL,opts)");
          exit(1);
     }  
}


//one_shot选择是否开启EPOLLONESHOT：
//ONESHOT选项表示只监听一次，监听完这次事件后，如果还需要监听
//，那就要再次把这个socket加入到EPOLL队列里;

//在EPOLLONESHOT模式下，对于同一个socket，每次读完或者写完之后
//再将其加入到EPOLL队列，就可以避免两个线程处理同一个socket
//（两个线程处理同一个soocket编码复杂性很高）
void addfd(int epollfd,int fd,bool one_shot)
{
	epoll_event event;
	event.data.fd = fd;
	//对于新增的event，默认监听ET和读
	event.events = EPOLLIN|EPOLLET|EPOLLRDHUP;

	if(one_shot)
		event.events |= EPOLLONESHOT;
	epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);

	setnonblocking(fd);
}

void modfd(int epollfd,int fd,int ev)
{
	epoll_event event;
	event.data.fd = fd;
	event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
	epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

int main(int argc,char**argv)
{
	if(argc <= 1)
	{
		perror("参数错误");
		return 1;
	}
	
	//忽略SIGPIPE
	signal(SIGPIPE, SIG_IGN);

	/*threadpool<http_conn> *pool = NULL;
	try
	{
		pool = new threadpool<http_conn>();
	}catch(std::exception& e)
	{
		perror("new threadpool error");
		return 1;
	}*/

	http_conn *users = new http_conn[MAX_FD/10];
	assert(users);
	http_conn::m_user_count = 0;

	int user_count = 0;
	int listenfd;

	//socket()
	listenfd = socket(AF_INET,SOCK_STREAM,0);
	assert(listen >= 0);

	struct sockaddr_in servaddr;
	bzero(&servaddr,sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(atoi(argv[1]));
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

	int ret = 0,flag = 1;
	//设置套接字选项，可参考《UNIX网络编程》第7.5节P165
	setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(flag));
	//bind()
	ret = bind(listenfd,(struct sockaddr*)&servaddr,sizeof(servaddr));
	assert(ret>=0);
	//listen()
	ret = listen(listenfd,5);
	assert(ret>=0);

	//创建内核事件表,用来接收epoll_wait
	//传出来的满足监听事件的fd结构体
	epoll_event events[MAX_EVENT_NUMBER];
	
	//epoll_create创建红黑树，epoll_create的参数在Linux2.6
	//之后就没什么用了，但是要大于0
	epollfd = epoll_create(5);
	assert(epollfd!=-1);
	
	//自己封装的epoll_ctl函数，往epollfd中加入listenfd
	addfd(epollfd,listenfd,false);
	http_conn::m_epollfd = epollfd;

	//epoll开始监听,采用ET（边缘触发）
	bool stop_server = false;
	
	int nready = 0;
	while(!stop_server)
	{
		//调用epoll_wait
		nready=epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
		if(nready<0&&errno != EINTR)
		{
			perror("epoll false");
			break;
		}
		
		//处理events
		for(int i=0;i < nready;i++)
		{
			int sockfd = events[i].data.fd;
			
			//listenfd上面有新连接，在ET模式下，需要将当前的
			//所有连接都处理,否则延迟会高
			if(sockfd == listenfd)
			{	
				struct sockaddr_in cliaddr;
				socklen_t clilen = sizeof(cliaddr);
				int connfd;
				
				while(1)
				{
					connfd=accept(listenfd,(struct sockaddr*)
											&cliaddr,&clilen);
					//所有连接都处理完毕
					if(connfd < 0)
					{
						printf("errno is %d\n",errno);
						break;
					}
					//用户数过多
					if(http_conn::m_user_count >= MAX_FD)
					{
						printf("用户数过多\n");
						break;
					}
					
				    //init初始化新接收的连接，并加入epoll监听列表
					users[connfd].init(connfd,cliaddr);
					continue;
				}
			}
			//关闭连接
			//else if(events[i].events&(EPOLLRDHUP|EPOLLHUP|EPOLLERR))
			
			//处理读事件
			else if(events[i].events&EPOLLIN)
			{
				if(users[sockfd].m_read())
				{
					printf("deal with the client %d\n", sockfd);
					users[sockfd].process();
					//pool->append(users+sockfd);
				}
			}
			//处理写事件
			else if(events[i].events&EPOLLOUT)
			{
				printf("EPOLLOUT\n");
				if(users[sockfd].m_write())
				{
					printf("send data to the client %d\n", sockfd);
				}
			}
		}
	}

	close(epollfd);
	close(listenfd);
	delete[]users;
	//delete pool;
	return 0;
}
