#include "httpconn.h"

const char* HttpConn::srcDir;
std::atomic<int> HttpConn::userCount;
bool HttpConn::isET;

HttpConn::HttpConn()
{
    m_fd = -1;
    m_addr = { 0 };
    m_isClose = true;
}

HttpConn::~HttpConn()
{
    Close();
}

void HttpConn::init(int fd, const sockaddr_in &addr)
{
    assert(fd > 0);
    userCount++;
    m_addr = addr;
    m_fd = fd;
    m_writeBuff.RetrieveAll();
    m_readBuff.RetrieveAll();
    m_isClose = false;
    LOG_INFO("Client[%d](%s:%d) in, userCount:%d", m_fd, GetIP(), GetPort(), (int)userCount);
}

ssize_t HttpConn::read(int *saveErrno)
{
    ssize_t len = -1;
    do {
        len = m_readBuff.ReadFd(m_fd, saveErrno);
        if (len <= 0) {
            break;
        }
    } while (isET);
    return len;
}

ssize_t HttpConn::write(int *saveErrno)
{
    ssize_t len = -1;
    do {
        // 分散写  响应头 + 响应file
        len = writev(m_fd, m_iov, m_iovCnt);
        if(len <= 0) {      //TCP写缓冲没有空间
            *saveErrno = errno;
            break;
        }
        if(m_iov[0].iov_len + m_iov[1].iov_len  == 0) { /* 传输结束 */
            break;
        }else if(static_cast<size_t>(len) > m_iov[0].iov_len) { // 发完头部了
            m_iov[1].iov_base = (uint8_t*) m_iov[1].iov_base + (len - m_iov[0].iov_len);        // 已经发了部分的响应体数据
            m_iov[1].iov_len -= (len - m_iov[0].iov_len);
            if(m_iov[0].iov_len) {
                m_writeBuff.RetrieveAll();
                m_iov[0].iov_len = 0;
            }
        }else {     // 还没发完头部
            m_iov[0].iov_base = (uint8_t*)m_iov[0].iov_base + len;
            m_iov[0].iov_len -= len;
            m_writeBuff.Retrieve(len);
        }
    } while(isET || ToWriteBytes() > 10240);
    return len;
}

void HttpConn::Close()
{
    m_response.UnmapFile();
    if(m_isClose == false){
        m_isClose = true;
        userCount--;
        close(m_fd);
        LOG_INFO("Client[%d](%s:%d) quit, UserCount:%d", m_fd, GetIP(), GetPort(), (int)userCount);
    }
}

int HttpConn::GetFd() const
{
    return m_fd;
}

int HttpConn::GetPort() const
{
    return m_addr.sin_port;
}

const char *HttpConn::GetIP() const
{
    return inet_ntoa(m_addr.sin_addr);
}

bool HttpConn::process()
{
    m_request.Init();
    if(m_readBuff.ReadableBytes() <= 0) {
        return false;
    }
    else if(m_request.parse(m_readBuff)) {      //解析成功,把数据已经放在wtriebuff里
        LOG_DEBUG("%s", m_request.path().c_str());
        m_response.Init(srcDir, m_request.path(), m_request.IsKeepAlive(), 200);     //返回响应状态，初始化
    } else {
        m_response.Init(srcDir, m_request.path(), false, 400);
    }

    m_response.MakeResponse(m_writeBuff);       //生成响应信息

    /* 响应头 */
    m_iov[0].iov_base = const_cast<char*>(m_writeBuff.Peek());
    m_iov[0].iov_len = m_writeBuff.ReadableBytes();
    m_iovCnt = 1;

    /* 文件 */
    if(m_response.FileLen() > 0  && m_response.File()) {
        m_iov[1].iov_base = m_response.File();
        m_iov[1].iov_len = m_response.FileLen();
        m_iovCnt = 2;
    }
    LOG_DEBUG("filesize:%d, %d  to %d", m_response.FileLen() , m_iovCnt, ToWriteBytes());
    return true;
}
