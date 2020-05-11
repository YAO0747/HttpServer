#include"http_conn.h"

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;


//定义http响应的一些状态信息
const char*ok_200_title = "OK";
const char* error_400_title = "Bab request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to statisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You don not have permission to get file from this server.\n";
const char* error_404_title = "Not found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the request line.\n";


void http_conn::init(int sockfd,const sockaddr_in &addr)
{
	m_sockfd = sockfd;
	m_addr = addr;
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

//当 EPOLL_CTL_ADD或EPOLL_CTL_MOD 时 , 如果socket输出缓冲区没满就能触发一次
void http_conn::process()
{
	HTTP_CODE ret = process_read();
	process_write(ret);
	modfd(m_epollfd,m_sockfd,EPOLLOUT);
}
//解析m_read_buf中的内容，每次读取一行
http_conn:: LINE_STATUS http_conn::parse_line()
{
	char text;

	for(;m_checked_idx < m_read_idx;++m_checked_idx)
	{
		//获得当前字节
		text = m_read_buf[m_checked_idx];

		//若为'\r'，则表示可能读到了一个完整的行
		if(text == '\r')
		{
			//若正好是buffer的最末尾的字节，则返回LINE_OPEN
			if(m_checked_idx == m_read_idx -1)
				return LINE_OPEN;
			//若下一个字节是'\n'，则说明读到了完整的行
			if(m_read_buf[m_checked_idx+1] == '\n')
			{
				//更新checked_index
				m_read_buf[m_checked_idx++] = '\0';
				m_read_buf[m_checked_idx++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD;
		}
		//若为'\n',也说明可能读到了一个完整的行
		else if(text == '\n')
		{
			if((m_checked_idx > 1)&&m_read_buf[m_checked_idx-1]=='\r')
			{	
				//更新checked_index
				m_read_buf[m_checked_idx-1] = '\0';
				m_read_buf[m_checked_idx++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD;
		}
	}
	return LINE_OPEN;

}
//解析请求行，参数text: 请求行（在m_read_buf中）的地址
http_conn:: HTTP_CODE http_conn::parse_requestline(char* text)
{
	//若请求行无空格或\t，则一定出错，因此先判断有无空格
	//m_url位置指向请求行text的第二个字段
	m_url = strpbrk(text, " \t");
    if (!m_url)
        return BAD_REQUEST;
    *m_url++ = '\0';

	//text的第一个字段是请求方法
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
        m_method = POST;
    else
        return BAD_REQUEST;

	//请求行有可能以多个空格分隔字段，所以m_url需要再次移动
    m_url += strspn(m_url, " \t");

	//version在第三个字段
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;

	//开始处理m_url,提取出/.....
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    /*
	//当url为/时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");*/
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}	

//解析请求头部，参数text: 该请求头部（在m_read_buf中）的地址
http_conn:: HTTP_CODE http_conn::parse_headers(char*text)
{
	//遇到空行，需要判断后面是否有请求体
	if (text[0] == '\0')
    {
        if (m_content_length != 0)
        {
			//如果有请求体，则需要更新状态
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
		//若无请求体，则解析完成
        return GET_REQUEST;
    }
	//处理Connection
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
	//处理Content-length
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
	//处理Host
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        printf("oop!unknow header: %s\n",text);
        //LOG_INFO("oop!unknow header: %s", text);
        //Log::get_instance()->flush();
    }
    return NO_REQUEST;
}

//读取请求体，暂不进行处理，参数text: 请求体（在m_read_buf中）的地址
http_conn:: HTTP_CODE http_conn::parse_content(char*text)
{
	//判断请求体是否已经全部在m_read_buf中
	if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        //POST请求中最后为输入的用户名和密码
        //m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//主状态机，处理整个请求
http_conn::HTTP_CODE http_conn::process_read()
{
	LINE_STATUS line_status = LINE_OK;//当前行的状态
	HTTP_CODE ret = NO_REQUEST;
	char * text = 0;

	//每次调用parse_line()获取一行，解析到content时无需再调用parse_line逐行获取
	while((m_check_state == CHECK_STATE_CONTENT&&line_status == LINE_OK)||((line_status = parse_line())==LINE_OK))
	{
		//得到当前行:buf首地址+行起始位置
		text = m_read_buf + m_start_line;
		m_start_line = m_checked_idx;//更新行起始位置

		switch(m_check_state)
		{
			case CHECK_STATE_REQUESTLINE:
			{
				ret = parse_requestline(text);
				if(ret==BAD_REQUEST)
					return BAD_REQUEST;
				break;
			}
			case CHECK_STATE_HEADER:
			{
				ret = parse_headers(text);
				if(ret == BAD_REQUEST)
					return BAD_REQUEST;
				else if(ret == GET_REQUEST)
					return do_request();
				break;
			}
			case CHECK_STATE_CONTENT:
			{
				ret = parse_content(text);
				if(ret==GET_REQUEST)
					return do_request();
				//如果parse_content返回值不为GET_REQUEST,则表示
				//m_read_buf中未包含完整的请求，需要继续监听并读取
				else
					line_status = LINE_OPEN;
				break;
			}
			default:
				return INTERNAL_ERROR;
		}
	}
	return NO_REQUEST;
}

//整个http请求被解析完成后，调用do_request进行处理（响应）
http_conn:: HTTP_CODE http_conn::do_request()
{
	return GET_REQUEST;	
}

//ret是process_read的返回值
http_conn::HTTP_CODE http_conn::process_write(HTTP_CODE ret)
{
	//switch(ret)
	//{
	//	case INTERNAL_ERROR:
	//	{
			
	//	}
	//}
	strcpy(m_write_buf,ok_200_title);
	return GET_REQUEST;
}
