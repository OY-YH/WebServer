#ifndef HTTPCONN_H
#define HTTPCONN_H

#include <sys/types.h>
#include <sys/uio.h>     // readv/writev
#include <arpa/inet.h>   // sockaddr_in
#include <stdlib.h>      // atoi()
#include <errno.h>
#include <atomic>       //类模板，原子操作

#include "buffer.h"
#include "httprequest.h"
#include "httpresponse.h"

class HttpConn
{
public:
    //共享资源
    static bool isET;                   //是否为ET模式
    static const char* srcDir;          //资源目录
    static std::atomic<int> userCount;  //客户端数量
public:
    HttpConn();

    ~HttpConn();

    void init(int fd, const sockaddr_in& addr);

    ssize_t read(int* saveErrno);

    ssize_t write(int* saveErrno);

    void Close();

    int GetFd() const;

    int GetPort() const;

    const char* GetIP() const;

    sockaddr_in GetAddr() const;

    bool process();

    int ToWriteBytes() {
        return m_iov[0].iov_len + m_iov[1].iov_len;
    }

    bool IsKeepAlive() const {
        return m_request.IsKeepAlive();
    }

private:

    int m_fd;
    struct  sockaddr_in m_addr;

    bool m_isClose;

    // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    //两块内存 响应头，响应正文 文件
    int m_iovCnt;
    struct iovec m_iov[2];

    Buffer m_readBuff; // 读（请求）缓冲区，保存请求数据的内容
    Buffer m_writeBuff; // 写缓冲区，保存响应数据的内容

    HttpRequest m_request;
    HttpResponse m_response;

};

#endif // HTTPCONN_H
