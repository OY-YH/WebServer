#include "http_conn.h"

// 类中静态成员需要外部定义
int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;
int http_conn::m_request_count = 0;
sort_timer_lst http_conn::m_timer_lst;

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

// 网站的根目录
const char * doc_root = "/run/media/root/study/C++work/webserver/resources";

//设置文件描述符非阻塞
void setnonblocking(int fd)
{
    int old_flag=fcntl(fd,F_GETFL);
    int new_flag=old_flag | O_NONBLOCK;
    fcntl(fd,F_SETFL,new_flag);
}
//向epoll中添加需要监听的文件描述符
void addfd(int epollfd,int fd,bool one_shot,bool et)
{
    epoll_event event;
    event.data.fd=fd;
//    event.events= EPOLLIN | EPOLLRDHUP;
//    event.events= EPOLLIN |EPOLLET| EPOLLRDHUP; //边缘触发，这里如果设置了，监听描述符也会变成ET模式，但一般不是设置成它，所以后面要处理

    if(et){
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;  // 对所有fd设置边沿触发，但是listen_fd不需要，可以另行判断处理
    }else{
        event.events = EPOLLIN | EPOLLRDHUP;    // 默认水平触发    对端连接断开触发的epoll 事件包含 EPOLLIN | EPOLLRDHUP挂起，不用根据返回值判断，直接通过事件判断异常断开
    }

    if(one_shot){
        // 防止同一个通信被不同的线程处理
        event.events |= EPOLLONESHOT;
    }

    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    //设置文件描述符非阻塞
    //（如果用的边沿触发模式，需要一次性b把数据都读出来，读的时候应该是非阻塞的
    // 如果是阻塞的，那没有数据就会阻塞
    setnonblocking(fd);

}

//从epoll中移除需要监听的文件描述符
void removefd(int epollfd,int fd)
{
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}

//修改文件描述符，重置socket上EPOLLONESHOT事件，以确保下一次可读时EPOLLIN事件能被触发
void modfd(int epollfd,int fd,int ev)
{
    epoll_event event;
    event.data.fd=fd;
    event.events=ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

http_conn::http_conn()
{

}

http_conn::~http_conn()
{

}

//有线程池的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process()       // 线程池中线程的业务处理
{
    EMlog(LOGLEVEL_DEBUG, "=======parse request, create response.=======\n");

    //解析HTTP请求
    EMlog(LOGLEVEL_DEBUG,"=============process_reading=============\n");
    HTTP_CODE read_ret=process_read();
    EMlog(LOGLEVEL_INFO,"========PROCESS_READ HTTP_CODE : %d========\n", read_ret);
    if(read_ret==NO_REQUEST){               //请求不完整
        modfd(m_epollfd,m_sockfd,EPOLLIN);  // 继续监听EPOLLIN （| EPOLLONESHOT）
        return ;                            // 返回，线程空闲
    }

//    printf("parse request,create response\n");

    //生成响应
    EMlog(LOGLEVEL_DEBUG,"=============process_writting=============\n");
    bool write_ret = process_write( read_ret );
    if ( !write_ret ) {
        close_conn();
        if(timer) m_timer_lst.del_timer(timer);  // 移除其对应的定时器
    }
    // 重置EPOLLONESHOT
    modfd( m_epollfd, m_sockfd, EPOLLOUT);

}

void http_conn::init(int sockfd, const sockaddr_in &addr)
{
    m_sockfd=sockfd;        // 套接字
    m_address=addr;         // 客户端地址

    //设置端口复用
    int reuse=1;
    setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    //添加到epoll对象中
    addfd(m_epollfd,m_sockfd,true,ET);
    m_user_count++;     //总用户数+1

    char ip[16] = "";
    const char* str = inet_ntop(AF_INET, &addr.sin_addr.s_addr, ip, sizeof(ip));
    EMlog(LOGLEVEL_INFO, "The No.%d user. sock_fd = %d, ip = %s.\n", m_user_count, sockfd, str);

    init();

    // 创建定时器，设置其回调函数与超时时间，然后绑定定时器与用户数据，最后将定时器添加到链表timer_lst中
    util_timer* new_timer = new util_timer;
    new_timer->user_data = this;
    time_t curr_time = time(NULL);
    new_timer->exprie = curr_time + 3 * TIMESLOT;
    this->timer = new_timer;
    m_timer_lst.add_timer(new_timer);
}

void http_conn::close_conn()
{
    if(m_sockfd!=-1){
        m_user_count--;     //关闭一个连接，总用户数-1
        EMlog(LOGLEVEL_INFO, "closing fd: %d, rest user num :%d\n", m_sockfd, m_user_count);
        removefd(m_epollfd,m_sockfd);
        m_sockfd=-1;
    }
}

//循环的读取客户数据，直到无数据刻度或者对方关闭连接
bool http_conn::read()
{
    if(timer) {             // 更新超时时间
        time_t curr_time = time( NULL );
        timer->exprie = curr_time + 3 * TIMESLOT;
        m_timer_lst.adjust_timer( timer );
    }

    if(m_read_idx>=READ_BUFFER_SIZE){       // 超过缓冲区大小
        return false;
    }

    //读取到的字节
    int byetes_read=0;
    //一次性读完是这个函数能一次性读完，读是在while里循环读的，并不是调用一次recv就全部读到了，所以要用idx记录赏赐读到的位置
    // m_sock_fd已设置非阻塞
    while(true){
        // 从m_read_buf + m_read_idx索引出开始保存数据，大小是READ_BUFFER_SIZE - m_read_idx
        byetes_read=recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);
        if(byetes_read==-1){
            if(errno==EAGAIN||errno==EWOULDBLOCK){
                //没有数据
                break;
            }
            return false;
        }else if(byetes_read==0){
            //对方关闭连接
            return false;
        }
        m_read_idx+=byetes_read;
    }

//    printf("读取到了数据：\n %s\n",m_read_buf);

    ++m_request_count;

    EMlog(LOGLEVEL_INFO, "sock_fd = %d read done. request cnt = %d\n", m_sockfd, m_request_count);    // 全部读取完毕

    return true;
}

bool http_conn::write()
{
    int temp = 0;

    if(timer) {             // 更新超时时间
        time_t curr_time = time( NULL );
        timer->exprie = curr_time + 3 * TIMESLOT;
        m_timer_lst.adjust_timer( timer );
    }

//    bytes_have_send = 0;    // 已经发送的字节
//    bytes_to_send = m_write_idx;// 将要发送的字节 （m_write_idx）写缓冲区中待发送的字节数

    EMlog(LOGLEVEL_INFO, "sock_fd = %d writing %d bytes. request cnt = %d\n", m_sockfd, bytes_to_send, m_request_count);

    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        modfd( m_epollfd, m_sockfd, EPOLLIN );
        init();
        return true;
    }

    while(1) {
        // 分散写  m_write_buf + m_file_address
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if ( temp <= -1 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                modfd( m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;

        if (bytes_have_send >= m_iv[0].iov_len){    // 发完头部了
            m_iv[0].iov_len = 0;                    // 更新两个发送内存块的信息
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);    // 已经发了部分的响应体数据
            m_iv[1].iov_len = bytes_to_send;
        }else{                                      // 还没发完头部
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if ( bytes_to_send <= 0 ) {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            // 没有数据要发送了
            unmap();
            modfd( m_epollfd, m_sockfd, EPOLLIN );
            if(m_linger) {
                init();
//                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return true;
            } else {
//                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return false;
            }
        }
    }
    return true;
}

void http_conn::init()
{
    m_checked_state=CHECK_STATE_REQUESTLINE;    //初始化状态为解析请求首行
    m_checked_idx=0;
    m_start_line=0;
    m_read_idx=0;

    m_write_idx = 0;
    bytes_have_send = 0;
    bytes_to_send = 0;

    m_method=GET;   // 默认请求方式为GET
    m_url=0;
    m_version=0;
    m_content_length = 0;
    m_host = 0;
    m_linger=false; //默认不保持链接  Connection : keep-alive保持连接

//    bzero(m_read_buf,READ_BUFFER_SIZE);         // 清空读缓存
    memset(m_read_buf,'\0',READ_BUFFER_SIZE);
//    bzero(m_write_buf, WRITE_BUFFER_SIZE);      // 清空写缓存
    memset(m_write_buf,'\0',WRITE_BUFFER_SIZE);
//    bzero(m_real_file, FILENAME_LEN);           // 清空文件路径
    memset(m_real_file,'\0', FILENAME_LEN);
}

//主状态机
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status=LINE_OK;
    HTTP_CODE ret=NO_REQUEST;
    //获取的一行数据
    char * text=0;

    // 主状态机正在解析请求体，且从状态机OK，不需要一行一行解析
    while(((m_checked_state==CHECK_STATE_CONTENT)&&(line_status==LINE_OK))
           ||((line_status=parse_line())==LINE_OK)){
        //解析到了一行完整的数据，或者解析到了请求体，也是完整的数据

        //获取一行数据
        text=get_line();

        m_start_line=m_checked_idx; // 更新下一行的起始位置

//        printf("got 1 http line: %s\n",text);
        EMlog(LOGLEVEL_DEBUG, ">>>>>> %s\n", text);

        switch(m_checked_state){
            case CHECK_STATE_REQUESTLINE:
            {
                ret=parse_request_line(text);
                if(ret==BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret=parse_request_headers(text);
                if(ret==BAD_REQUEST){
                    return BAD_REQUEST;
                }else if(ret==GET_REQUEST){
                    return do_request();        // 解析具体的请求信息
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret=parse_request_content(text);
                if(ret==GET_REQUEST){
                    return do_request();        // 解析具体的请求信息
                }
                line_status=LINE_OPEN;

                break;
            }
            default:
                return INTERNAL_ERROR;          //内部错误
        }
    }
    return NO_REQUEST;       // 数据不完整
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
        add_status_line( 500, error_500_title );
        add_headers( strlen( error_500_form ) ,time(NULL));
        if ( ! add_content( error_500_form ) ) {
                return false;
        }
        break;
    case BAD_REQUEST:
        add_status_line( 400, error_400_title );
        add_headers( strlen( error_400_form ) ,time(NULL));
        if ( ! add_content( error_400_form ) ) {
                return false;
        }
        break;
    case NO_RESOURCE:
        add_status_line( 404, error_404_title );
        add_headers( strlen( error_404_form ) ,time(NULL));
        if ( ! add_content( error_404_form ) ) {
                return false;
        }
        break;
    case FORBIDDEN_REQUEST:
        add_status_line( 403, error_403_title );
        add_headers(strlen( error_403_form),time(NULL));
        if ( ! add_content( error_403_form ) ) {
                return false;
        }
        break;
    case FILE_REQUEST:
        add_status_line(200, ok_200_title );
        add_headers(m_file_stat.st_size,time(NULL));
        EMlog(LOGLEVEL_DEBUG, "<<<<<<< %s", m_file_address);
        // 封装m_iv
        m_iv[ 0 ].iov_base = m_write_buf;
        m_iv[ 0 ].iov_len = m_write_idx;
        m_iv[ 1 ].iov_base = m_file_address;
        m_iv[ 1 ].iov_len = m_file_stat.st_size;
        m_iv_count = 2; // 两块内存
        bytes_to_send = m_write_idx + m_file_stat.st_size;  // 响应头的大小 + 文件的大小
        return true;
    default:
        return false;
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

// 解析HTTP请求行，获得请求方法，目标URL,以及HTTP版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    //   GET / HTTP/1.1
    m_url=strpbrk(text," \t");  // 判断第二个参数中的字符哪个在text中最先出现

    //   GET\0/ HTTP/1.1
    *m_url++='\0';  // 置位空字符，字符串结束符

    char * method=text;
    if(strcasecmp(method,"GET")==0){    // 忽略大小写比较
        m_method=GET;
    }else{
        return BAD_REQUEST;
    }

    //   / HTTP/1.1
    // 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。
    m_version=strpbrk(m_url," \t");
    if(!m_version){
        return BAD_REQUEST;
    }

    //   /\0HTTP/1.1
    *m_version++='\0';
    m_version++;        // HTTP/1.1

    // 非HTTP1.1版本，压力测试时为1.0版本，忽略改行
//    if(strcasecmp(m_version,"HTTP/1.1")!=0){
//        return BAD_REQUEST;
//    }

//    http://172.20.10.3:10000/
    if(strncasecmp(m_url,"http://",7)==0){
        m_url+=7;   // 172.20.10.3:10000/
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        m_url=strchr(m_url,'/');    // /

    }
    if(!m_url||m_url[0]!='/'){
        return BAD_REQUEST;
    }

    m_checked_state=CHECK_STATE_HEADER;     //主状态机检查状态变为检查请求头
    return NO_REQUEST;
}

// 解析HTTP请求的一个头部信息
// 在枚举类型前加上 `http_conn::` 来指出它的所属作用域
http_conn::HTTP_CODE http_conn::parse_request_headers(char *text)
{
    // 遇到空行，表示头部字段解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if ( m_content_length != 0 ) {      // 请求体有内容
                m_checked_state = CHECK_STATE_CONTENT;
                return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );      // 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
                m_linger = true;
        }
    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    } else {
//        printf( "oop! unknow header %s\n", text );
        #ifdef COUT_OPEN
            EMlog(LOGLEVEL_DEBUG,"oop! unknow header: %s\n", text );
        #endif
    }
    return NO_REQUEST;
}

// 我们没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入了
http_conn::HTTP_CODE http_conn::parse_request_content(char *text)
{
    if ( m_read_idx >= ( m_content_length + m_checked_idx ) )   // 读到的数据长度 大于 已解析长度（请求行+头部+空行）+请求体长度
    {   // 数据被完整读取
        text[ m_content_length ] = '\0';        // 标志结束
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//解析一行，判断依据/r/n
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;

    for(;m_checked_idx<m_read_idx;++m_checked_idx){
        //temp为将要分析的字节
        temp=m_read_buf[m_checked_idx];
        //如果当前是\r字符,则有可能读取到完整行
        if(temp=='\r'){
                //下一个字符达到了buffer结尾，则接收不完整，需要继续接受
            if((m_checked_idx+1)==m_read_idx){
                return LINE_OPEN;
            }else if(m_read_buf[m_checked_idx+1]=='\n'){    //下一个字符是\n,将\r\n改为\0\0
                m_read_buf[m_checked_idx++]='\0';
                m_read_buf[m_checked_idx++]='\0';
                return LINE_OK;
            }
            //都不符合
            return LINE_BAD;
        }else if(temp=='\n'){   //由于第一次read读到的最后一个字符是\r,此时服务器还没收到后面的数据，就会导致第二次read读到的第一个字符是\n   刚好\r \n 在不同数据的结尾和开头的情况
            if(m_checked_idx>1 && m_read_buf[m_checked_idx-1]=='\r'){   //前一个字符是\r,接收完整
                m_read_buf[m_checked_idx-1]='\0';
                m_read_buf[m_checked_idx++]='\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    //并没有找到\r\n,需要继续接受
    return LINE_OPEN;
}

// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request()
{
    // "/run/media/root/study/C++work/webserver/resources"
    strcpy( m_real_file, doc_root );
    int len = strlen( doc_root );
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );    //拼接成真实文件
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if ( stat( m_real_file, &m_file_stat ) < 0 ) {
        return NO_RESOURCE;
    }

    // 判断访问权限（有没有读的权限
    if ( ! ( m_file_stat.st_mode & S_IROTH ) ) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if ( S_ISDIR( m_file_stat.st_mode ) ) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open( m_real_file, O_RDONLY );
    // 创建内存映射
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    return FILE_REQUEST;
}

// 对内存映射区执行munmap操作 释放
void http_conn::unmap()
{
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response(const char *format, ...)    // 可变参数列表
{
    if( m_write_idx >= WRITE_BUFFER_SIZE ) {    // 写缓冲区满了
        return false;
    }
    va_list arg_list;   //arg_list负责接受可变参数列表
    va_start( arg_list, format );   // 通过format来对arg_list进行初始化
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {
        return false;       // 没写完，已经满了
    }
    m_write_idx += len;     // 更新下次写数据的起始位置
    va_end( arg_list );
    return true;
}

bool http_conn::add_content(const char *content)
{
    EMlog(LOGLEVEL_DEBUG,"<<<<<<< %s\n", content );
    return add_response( "%s", content );
}

bool http_conn::add_content_type()
{
    //根据类型去写，不一定是text/html
    // 区分是图片 / html/css
//    char *format_file = strrchr(m_filename, '.');
//    return add_response("Content-Type: %s\r\n", format_file == NULL ? "text/html" : (format_file + 1));
    EMlog(LOGLEVEL_DEBUG,"<<<<<<< Content-Type:%s\r\n", "text/html");
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_status_line(int status, const char *title)
{
    EMlog(LOGLEVEL_DEBUG,"<<<<<<< %s %d %s\r\n", "HTTP/1.1", status, title);
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool http_conn::add_headers(int content_length,time_t time)
{
    if (!add_content_length(content_length))
        return false;
    if (!add_content_type())
        return false;
    if(!add_linger())
        return false;
    if (!add_date(time)) return false;
    if (!add_blank_line()) return false;
    return true;
}

bool http_conn::add_content_length(int content_length)
{
    EMlog(LOGLEVEL_DEBUG,"<<<<<<< Content-Length: %d\r\n", content_length);
    return add_response( "Content-Length: %d\r\n", content_length );
}

bool http_conn::add_linger()
{
    EMlog(LOGLEVEL_DEBUG,"<<<<<<< Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
    EMlog(LOGLEVEL_DEBUG,"<<<<<<< %s", "\r\n" );
    return add_response( "%s", "\r\n" );
}

//发送时间
bool http_conn::add_date(time_t t)
{
    char timebuf[50];
    strftime(timebuf, 80, "%Y-%m-%d %H:%M:%S", localtime(&t));
    EMlog(LOGLEVEL_DEBUG,"<<<<<<< Date: %s\r\n", timebuf );
    return add_response("Date: %s\r\n", timebuf);
}
