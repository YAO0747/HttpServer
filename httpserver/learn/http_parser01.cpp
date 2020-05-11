#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>



#define BUFFER_SIZE 4096

//主状态机的三种状态：正在分析请求行，正在分析请求头部，正在分
//析请求内容
enum CHECK_STATE{CHECK_STATE_REQUESTLINE = 0,CHECK_STATE_HEADER,CHECK_STATE_CONTENT};

//从状态机：读取到一个完整的行，行出错，行数据尚未完整
//(主状态机中的每种状态又可以分为三种从状态)
enum LINE_STATUS{LINE_OK = 0,LINE_BAD,LINE_OPEN};

//服务器处理HTTP请求的结果：
//NO_REQUEST:请求数据不完整,需要继续获取请求数据；
//GET_REQUEST：获得了一个完整的请求；
//BAD_REQUEST：用户请求语法错误；
//FORBIDDEN_REQUEST：对资源没有访问权限
//INTERNAL_ERROR:服务器内部错误
//CLOSED_CONNECTION:客户已经关闭连接了
enum HTTP_CODE{NO_REQUEST,GET_REQUEST,BAD_REQUEST,FORBIDDEN_REQUEST,INTERNAL_ERROR,CLOSE_CONNECTION};

static const char* szret[] = {"I get a correct result\n","something wrong"};

//从状态机，每次读取一行（如果没有语法错误的话）
LINE_STATUS parse_line(char* buffer,int &checked_index,int &read_index)
{
	//check_index指向buffer（应用程序缓冲区）中当前正在分析的字
	//节，read_index指向buffer中客户数据的尾部的下一字节
	//我们将要解析的是buffer中的checked_index到read_index-1字节
	char temp;

	for(;checked_index < read_index;++checked_index)
	{
		//获得当前字节
		temp = buffer[checked_index];

		//若为'\r'，则表示可能读到了一个完整的行
		if(temp == '\r')
		{
			//若正好是buffer的最末尾的字节，则返回LINE_OPEN
			if(checked_index == read_index -1)
				return LINE_OPEN;
			//若下一个字节是'\n'，则说明读到了完整的行
			if(buffer[checked_index+1] == '\n')
			{
				//更新checked_index
				buffer[checked_index++] = '\0';
				buffer[checked_index++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD;
		}
		//若为'\n',也说明可能读到了一个完整的行
		else if(temp == '\n')
		{
			if((checked_index > 1)&&buffer[checked_index-1]=='\r')
			{	
				//更新checked_index
				buffer[checked_index-1] = '\0';
				buffer[checked_index++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD;
		}
	}
	return LINE_OPEN;
}

//服务器分析请求行
HTTP_CODE parse_requestline(char*temp,CHECK_STATE& checkstate)
{
	//'\t'(制表符)或者空格用于分隔http的内容
	//检查请求行中temp是否有\t或者空格，若无,则http请求语法出错
	//strpbrk函数返回第一个/t的位置，因此url就指向了该位置
	char *url = strpbrk(temp," \t");
	if(!url)
		return BAD_REQUEST;
	//删掉第一个\t或空格
	*url++ = '\0';
	
	//支持GET方法
	char*method = temp;
	if(strcasecmp(method,"GET")==0)
	{
		printf("request method is GET\n");
	}
	else
		return BAD_REQUEST;
	
	//strspn返回url中第一个不是\t或者空格的字符的位置下标，调用
	//该函数的原因是http可能用两个空格(或\t)来分隔内容；
	//url会偏移到该位置,也就是url的开始字符
	url += strspn(url," \t");
	
	//verion指向下一个\t的位置
	char* version = strpbrk(url," \t");
	if(!version)
		return BAD_REQUEST;
	//删掉第二个\t
	*version++ = '\0';
	//version偏移到版本字段的开头
	version += strspn(version," \t");
	
	//支持http1.1
	if(strcasecmp(version,"HTTP/1.1") !=0)
		return BAD_REQUEST;

	//URL的格式为'/...',若开头为http://，则需要变换URL的位置
	if(strncasecmp(url,"http://",7)==0)
	{
		url += 7;
		//类似strpbrk，但是第二个参数是一个字符
		url = strchr(url,'/');
	}
	//检查url中是否有'/'
	if(!url||url[0]!='/')
		return BAD_REQUEST;
	
	printf("The request URL is: %s\n",url);
	checkstate = CHECK_STATE_HEADER;
	return NO_REQUEST;
}

HTTP_CODE parse_headers(char*temp)
{
	//遇到一个空行，说明得到了一个正确的HTTP请求
	if(temp[0]=='\0')
		return GET_REQUEST;
	//现只处理HOST字段
	else if(strncasecmp(temp,"Host:",5)==0)
	{
		temp += 5;
		temp += strspn(temp," \t");
		printf("the request host is %s\n",temp);
	}
	else
	{
		printf("暂不处理该类型的请求头部\n");
	}
	//继续处理请求头
	return NO_REQUEST;
}

//分析HTTP请求的入口函数
HTTP_CODE 
parse_content(char * buffer,int &checked_index,	CHECK_STATE& 
			  checkstate,int &read_index,int &start_line)
{
	LINE_STATUS linestatus = LINE_OK;//记录当前行的读取状态
	HTTP_CODE retcode = NO_REQUEST;//表示请求未接收完成


	while((linestatus = parse_line(buffer,checked_index,read_index)) == LINE_OK)
	{
		char*temp = buffer + start_line;//start_line是行在buffer中的位置
		start_line = checked_index;//更新下一行的起始位置
		

		//checkstate记录主状态机的状态
		switch(checkstate)
		{
		   case CHECK_STATE_REQUESTLINE://第一个状态,分析请求行
			{
				retcode = parse_requestline(temp,checkstate);
				if(retcode == BAD_REQUEST)
					return BAD_REQUEST;
				break;
			}
			case CHECK_STATE_HEADER:
			{
				retcode = parse_headers(temp);
				if(retcode==BAD_REQUEST)
					return BAD_REQUEST;
				else if(retcode == GET_REQUEST)
					return GET_REQUEST;
				break;
			}
			default:
			{
				return INTERNAL_ERROR;//服务器内部错误
			}
		}
	}

	if(linestatus == LINE_OPEN)
		return NO_REQUEST;
	else
		return BAD_REQUEST;
}

int main(int argc,char*argv[])
{
	if( argc <= 2 )
    {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi( argv[2] );
   
    //printf("port : %d",port);	
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );
    
    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );
    
    int ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret != -1 );
    
    int  ret2 = listen( listenfd, 5 );
    assert( ret2 != -1 );
    
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof( client_address );
    int fd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
    if( fd < 0 )
    {
        printf( "errno is: %d\n", errno );
    }
	else
	{
		char buffer[BUFFER_SIZE];
		memset(buffer,'\0',BUFFER_SIZE);
		int data_read = 0;
		int read_index = 0;//缓冲区buffer中的字节数
		int checked_index = 0;
		int start_line = 0;
		CHECK_STATE checkstate = CHECK_STATE_REQUESTLINE;
		while(1)
		{
			data_read = read(fd,buffer+read_index,BUFFER_SIZE-read_index);
			if(data_read == -1)
			{
				printf("reading err\n");
				break;
			}

			read_index += data_read;
			HTTP_CODE result = parse_content(buffer,checked_index,checkstate,read_index,start_line);

			if(result == NO_REQUEST)
				continue;
			else if(result == GET_REQUEST)
			{
				send(fd,szret[0],strlen(szret[0]),0);
				break;
			}
			else
			{
				send(fd,szret[1],strlen(szret[1]),0);
				break;
			}
		}
		close(fd);
	}
	close(listenfd);
	return 0;
}
