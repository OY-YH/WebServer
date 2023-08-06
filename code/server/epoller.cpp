#include "epoller.h"

Epoller::Epoller(int maxEvent):m_epollFd(epoll_create(512)), m_events(maxEvent)
{
    assert(m_epollFd >= 0 && m_events.size() > 0);
}

Epoller::~Epoller() {
    close(m_epollFd);
}

bool Epoller::AddFd(int fd, uint32_t events) {
    if(fd < 0) return false;
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    return 0 == epoll_ctl(m_epollFd, EPOLL_CTL_ADD, fd, &ev);
}

bool Epoller::ModFd(int fd, uint32_t events) {
    if(fd < 0) return false;
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    return 0 == epoll_ctl(m_epollFd, EPOLL_CTL_MOD, fd, &ev);
}

bool Epoller::DelFd(int fd) {
    if(fd < 0) return false;
    epoll_event ev = {0};
    return 0 == epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, &ev);
}

// 检测事件
int Epoller::Wait(int timeoutMs) {
    // 阻塞，返回事件数量
    return epoll_wait(m_epollFd, &m_events[0], static_cast<int>(m_events.size()), timeoutMs);
}

int Epoller::GetEventFd(size_t i) const {
    assert(i < m_events.size() && i >= 0);
    return m_events[i].data.fd;
}

uint32_t Epoller::GetEvents(size_t i) const {
    assert(i < m_events.size() && i >= 0);
    return m_events[i].events;
}
