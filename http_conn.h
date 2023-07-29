#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/epoll.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>
#include <string.h>

#include "locker.h"
#include "noactive/lst_timer.h"

class sort_timer_lst;
class util_timer;

const bool ET = true;
#define TIMESLOT 5      // 定时器周期：秒

// http 连接的用户数据类
class http_conn
{
public:
    // 共享对象，没有线程竞争资源，所以不需要互斥
    static int m_epollfd;               // 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
    static int m_user_count;            //统计用户的数量

    static sort_timer_lst m_timer_lst;  // 定时器链表
    util_timer* timer;                  // 定时器

public:
    static const int FILENAME_LEN = 200;        // 文件名的最大长度
    static const int READ_BUFFER_SIZE=2048;     //读缓冲区的大小
    static const int WRITE_BUFFER_SIZE=1024;    //写缓冲区的大小

   //这个后面还是封装到另一个类里去
    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};

    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };

    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完整的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };

    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

public:
    http_conn();
    ~http_conn();

    //处理客户端的请求，解析请求，响应
    void process();

    //初始化新接收的连接
    void init(int sockfd,const sockaddr_in & addr);
    //关闭连接
    void close_conn();

    //非阻塞的读
    bool read();
    //非阻塞的写
    bool write();

private:
    //初始化连接其余的信息(请求状态等
    void init();
    //解析http请求（主状态机
    HTTP_CODE process_read();
    // 填充HTTP应答
    bool process_write(HTTP_CODE ret);

    /*
     *下面这一组函数被process_read调用以分析HTTP请求
    */
    //解析http请求行，获得请求方法，目标URL,HTTP版本
    HTTP_CODE parse_request_line(char * text);
    //解析http请求头
    HTTP_CODE parse_request_headers(char * text);
    //解析http请求体
    HTTP_CODE parse_request_content(char * text);

    //解析具体的某一行
    LINE_STATUS parse_line();

    HTTP_CODE do_request();

    //获取一行数据
    char * get_line(){
        return m_read_buf+m_start_line;
    }

    /*
     * 这一组函数被process_write调用以填充HTTP应答。
    */
    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type();
    //添加响应行
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length,time_t time);
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();
    bool add_date(time_t t);

private:
    int m_sockfd;                           //该http连接的socket
    sockaddr_in m_address;                  //通信的socket地址

    char m_read_buf[READ_BUFFER_SIZE];      //读缓冲区
    int m_read_idx;                         //标识读缓冲区中以及读入的客户端数据的最后一个字节的下一个位置

    char m_write_buf[ WRITE_BUFFER_SIZE ];  // 写缓冲区
    int m_write_idx;                        // 写缓冲区中待发送的字节数

    int m_checked_idx;                      //当前正在分析的字符在读缓冲区的位置
    int m_start_line;                       //当前正在解析的行的起始位置

    CHECK_STATE m_checked_state;            //主状态机当前所处的状态

    //请求行信息的封装
    char * m_url;                           //请求目标文件的文件名
    char * m_version;                       //协议版本，只支持HTTP1.1
    METHOD m_method;                        //请求方法
    //请求头信息的封装
    char * m_host;                          //主机名
    bool m_linger;                          //判断http请求是否要保持连接
    int m_content_length;                   // HTTP请求体的消息总长度

    char m_real_file[ FILENAME_LEN ];       // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录
    struct stat m_file_stat;                // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    char* m_file_address;                   // 客户请求体的目标文件被mmap到内存中的起始位置

    // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    struct iovec m_iv[2];                   //两块内存：一块write_buf,另一块file_address（请求体的
    int m_iv_count;

    int bytes_have_send = 0;                // 已经发送的字节
    int bytes_to_send;        // 将要发送的字节 （m_write_idx）写缓冲区中待发送的字节数
};

#endif // HTTP_CONN_H
