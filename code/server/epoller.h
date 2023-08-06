#ifndef EPOLLER_H
#define EPOLLER_H

/*
    将IO复用epoll（检测事件发生，同时检测多个事件）封装起来
*/

#include <sys/epoll.h>  //epoll_ctl()
#include <fcntl.h>      // fcntl()
#include <unistd.h>     // close()
#include <assert.h>     // close()
#include <vector>
#include <errno.h>

class Epoller
{
public:
    explicit Epoller(int maxEvent = 1024);

    ~Epoller();

    //添加文件描述符到epoll中
    bool AddFd(int fd, uint32_t events);
    //修改文件描述符
    bool ModFd(int fd, uint32_t events);
    //删除文件描述符
    bool DelFd(int fd);

    int Wait(int timeoutMs = -1);

    int GetEventFd(size_t i) const;

    uint32_t GetEvents(size_t i) const;

private:
    int m_epollFd;

    std::vector<struct epoll_event> m_events;
};

#endif // EPOLLER_H
