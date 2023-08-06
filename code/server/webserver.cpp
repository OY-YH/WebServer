#include "webserver.h"

WebServer::WebServer(int port, int trigMode, int timeoutMS, bool OptLinger, int sqlPort, const char *sqlUser, const char *sqlPwd, const char *dbName, int connPoolNum, int threadNum, bool openLog, int logLevel, int logQueSize):
    m_port(port), m_openLinger(OptLinger), m_timeoutMS(timeoutMS), m_isClose(false),
    m_timer(new HeapTimer()), m_threadpool(new ThreadPool(threadNum)), m_epoller(new Epoller())
{
    m_srcDir= getcwd(nullptr, 256);         //获取当前的工作路径,通过动态内存分配函数（malloc)来为结果字符串分配内存
    assert(m_srcDir);
//    std::cout<<m_srcDir<<std::endl;

    strncat(m_srcDir, "/resources/", 16);     //拼接
    HttpConn::userCount = 0;
    HttpConn::srcDir = m_srcDir;

    SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    //初始化事件模式
    M_InitEventMode(trigMode);
    //初始化套接字
    if(!M_InitSocket()) {
        m_isClose = true;
    }

    if(openLog) {
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize);       //logquesize如果为0,不用异步，用同步
        if(m_isClose) { LOG_ERROR("========== Server init error!=========="); }
        else {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", m_port, OptLinger? "true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                     (m_listenEvent & EPOLLET ? "ET": "LT"),
                     (m_connEvent & EPOLLET ? "ET": "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}

WebServer::~WebServer()
{
    close(m_listenFd);
    m_isClose = true;
    free(m_srcDir);     //释放通过动态内存分配函数分配的内存,
    m_srcDir=nullptr;
    SqlConnPool::Instance()->ClosePool();
}

void WebServer::Start()
{
    int timeMS = -1;  /* epoll wait timeout == -1 无事件将阻塞 */

    if(!m_isClose) {        //没有关闭（服务器标志位）
        LOG_INFO("========== Server start ==========");
    }

    //循环等待监听/连接socket上的事件
    while(!m_isClose) {
        if(m_timeoutMS > 0) {
            timeMS = m_timer->GetNextTick();    //将要超时的时间 ,清除超时的客户端
        }
        // 检测事件
        int eventCnt = m_epoller->Wait(timeMS);      // 阻塞，返回事件数量 timeMS 防止一直阻塞

        //循环遍历事件数组
        for(int i = 0; i < eventCnt; i++) {
            /* 处理事件 */
            int fd = m_epoller->GetEventFd(i);
            uint32_t events = m_epoller->GetEvents(i);
            if(fd == m_listenFd) {
                // 监听文件描述符的事件响应   //有客户端连接进来
                M_DealListen();       //处理监听的操作，接受客户端连接
            }
            else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                //对方异常断开或者错误等事件
                assert(m_users.count(fd) > 0);
                M_CloseConn(&m_users[fd]);
            }
            else if(events & EPOLLIN) {
                //有读的事件发生
                assert(m_users.count(fd) > 0);
                M_DealRead(&m_users[fd]);
            }
            else if(events & EPOLLOUT) {
                //写事件发生
                assert(m_users.count(fd) > 0);
                M_DealWrite(&m_users[fd]);
            } else {
                LOG_ERROR("Unexpected event");
            }
        }
    }
}

//创建listenfd
bool WebServer::M_InitSocket()
{
    int ret;
    struct sockaddr_in addr;
    if(m_port > 65535 || m_port < 1024) {
        LOG_ERROR("Port:%d error!",  m_port);
        return false;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(m_port);

    struct linger optLinger = { 0 };
    if(m_openLinger) {
        /* 优雅关闭: 直到所剩数据发送完毕或超时 */
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }

    //创建
    m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if(m_listenFd < 0) {         //没有创建成功
        LOG_ERROR("Create socket error!", m_port);
        return false;
    }

    ret = setsockopt(m_listenFd, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if(ret < 0) {
        close(m_listenFd);
        LOG_ERROR("Init linger error!", m_port);
        return false;
    }

    int optval = 1;
    /* 端口复用 */
    /* 只有最后一个套接字会正常接收数据。 */
    ret = setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if(ret == -1) {
        LOG_ERROR("set socket setsockopt error !");
        close(m_listenFd);
        return false;
    }

    //绑定
    ret = bind(m_listenFd, (struct sockaddr *)&addr, sizeof(addr));
    if(ret < 0) {
        LOG_ERROR("Bind Port:%d error!", m_port);
        close(m_listenFd);
        return false;
    }

    //监听
    ret = listen(m_listenFd, 6);
    if(ret < 0) {
        LOG_ERROR("Listen port:%d error!", m_port);
        close(m_listenFd);
        return false;
    }

    //将监听的文件描述符添加到epoll对象中
    ret = m_epoller->AddFd(m_listenFd,  m_listenEvent | EPOLLIN);
    if(ret == 0) {
        LOG_ERROR("Add listen error!");
        close(m_listenFd);
        return false;
    }

    //设置监听描述符非阻塞
    SetFdNonblock(m_listenFd);
    LOG_INFO("Server port:%d", m_port);
    return true;
}

void WebServer::M_InitEventMode(int trigMode)
{
    m_listenEvent = EPOLLRDHUP;  // 监听文件描述符不需要 ONESHOT & ET ，如果选择了ET,后续的函数会循环处理
    m_connEvent = EPOLLONESHOT | EPOLLRDHUP;
    switch (trigMode)
    {
    case 0:
        break;
    case 1:
        m_connEvent |= EPOLLET;
        break;
    case 2:
        m_listenEvent |= EPOLLET;
        break;
    case 3:
        m_listenEvent |= EPOLLET;
        m_connEvent |= EPOLLET;
        break;
    default:
        m_listenEvent |= EPOLLET;
        m_connEvent |= EPOLLET;
        break;
    }
    HttpConn::isET = (m_connEvent & EPOLLET);
}

void WebServer::M_AddClient(int fd, sockaddr_in addr)
{
    assert(fd > 0);
    //将新的客户的数据初始化
    m_users[fd].init(fd, addr);
    //将定时器加入到客户端中
    if(m_timeoutMS > 0) {
        m_timer->add(fd, m_timeoutMS, std::bind(&WebServer::M_CloseConn, this, &m_users[fd]));
    }
    //添加到epoll里，方便ta检测
    m_epoller->AddFd(fd, EPOLLIN | m_connEvent);
    SetFdNonblock(fd);
    LOG_INFO("Client[%d] in!", m_users[fd].GetFd());
}

void WebServer::M_DealListen()
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    do {
        int fd = accept(m_listenFd, (struct sockaddr *)&addr, &len);
        if(fd <= 0) {       //没有客户端连接，返回-1
            return;         //退出
        }
        else if(HttpConn::userCount >= MAX_FD) {    //满了
            M_SendError(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
        //添加客户端的数据
        M_AddClient(fd, addr);
    } while(m_listenEvent & EPOLLET);       //监听描述符是ET模式，需要循环处理连接
}

void WebServer::M_DealWrite(HttpConn *client)
{
    assert(client);
    M_ExtentTime(client);
    m_threadpool->AddTask(std::bind(&WebServer::M_OnWrite, this, client));
}

void WebServer::M_DealRead(HttpConn *client)
{
    assert(client);
    M_ExtentTime(client);       //延长超时时间
    //交给子线程去处理
    m_threadpool->AddTask(std::bind(&WebServer::M_OnRead, this, client));
}

void WebServer::M_SendError(int fd, const char *info)
{
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if(ret < 0) {
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);
}

void WebServer::M_ExtentTime(HttpConn *client)
{
    assert(client);
    if(m_timeoutMS > 0) {
        m_timer->adjust(client->GetFd(), m_timeoutMS);
    }
}

void WebServer::M_CloseConn(HttpConn *client)
{
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    m_epoller->DelFd(client->GetFd());
    client->Close();
}

//在子线程中执行
void WebServer::M_OnRead(HttpConn *client)
{
    assert(client);
    int ret = -1;
    int readErrno = 0;
    ret = client->read(&readErrno);     //读取客户端的数据
    if(ret <= 0 && readErrno != EAGAIN) {
        M_CloseConn(client);
        return;
    }
    //业务逻辑的处理
    M_OnProcess(client);
}

void WebServer::M_OnWrite(HttpConn *client)
{
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    ret = client->write(&writeErrno);
    if(client->ToWriteBytes() == 0) {
        /* 传输完成 */
        if(client->IsKeepAlive()) {
            M_OnProcess(client);
            return;
        }
    }
    else if(ret < 0) {
        if(writeErrno == EAGAIN) {
            /* 继续传输 */
            m_epoller->ModFd(client->GetFd(), m_connEvent | EPOLLOUT);
            return;
        }
    }
    M_CloseConn(client);
}

void WebServer::M_OnProcess(HttpConn *client)
{
    if(client->process()) {     //处理业务逻辑成功，修改通信描述符是可写的事件
        m_epoller->ModFd(client->GetFd(), m_connEvent | EPOLLOUT);
    } else {
        m_epoller->ModFd(client->GetFd(), m_connEvent | EPOLLIN);
    }
}

int WebServer::SetFdNonblock(int fd)
{
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}
