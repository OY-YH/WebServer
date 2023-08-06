#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <unordered_map>
#include <fcntl.h>       // fcntl()
#include <unistd.h>      // close()
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "http/httpconn.h"
#include "pool/threadpool.h"
#include "epoller.h"
#include "noactive/heaptimer.h"
#include "Log/log.h"

class WebServer
{
public:
    WebServer(
        int port, int trigMode, int timeoutMS, bool OptLinger,
        int sqlPort, const char* sqlUser, const  char* sqlPwd,
        const char* dbName, int connPoolNum, int threadNum,
        bool openLog, int logLevel, int logQueSize);

    ~WebServer();
    void Start();

private:
    bool M_InitSocket();
    void M_InitEventMode(int trigMode);
    void M_AddClient(int fd, sockaddr_in addr);

    void M_DealListen();
    void M_DealWrite(HttpConn* client);
    void M_DealRead(HttpConn* client);

    void M_SendError(int fd, const char*info);
    void M_ExtentTime(HttpConn* client);        //延长超时时间
    void M_CloseConn(HttpConn* client);

    void M_OnRead(HttpConn* client);
    void M_OnWrite(HttpConn* client);
    void M_OnProcess(HttpConn* client);

private:
    static const int MAX_FD = 65536;
    // 文件描述符设置非阻塞操作
    static int SetFdNonblock(int fd);

private:
    int m_port;
    bool m_openLinger;
    int m_timeoutMS;  /* 毫秒MS */
    bool m_isClose;
    int m_listenFd;
    char* m_srcDir;

    uint32_t m_listenEvent;
    uint32_t m_connEvent;

    std::unique_ptr<HeapTimer> m_timer;         //计时器
    std::unique_ptr<ThreadPool> m_threadpool;   //线程池
    std::unique_ptr<Epoller> m_epoller;         //检测时间
    std::unordered_map<int, HttpConn> m_users;  //保存的是客户端连接的信息 一个连接进来就保存在这里面 <fd,客户端连接信息>
};

#endif // WEBSERVER_H
