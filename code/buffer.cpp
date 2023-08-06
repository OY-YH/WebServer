#include "buffer.h"

Buffer::Buffer(int initBuffSize): m_buffer(initBuffSize), m_readPos(0), m_writePos(0)
{

}

size_t Buffer::WritableBytes() const
{
    return m_buffer.size() - m_writePos;
}

size_t Buffer::ReadableBytes() const
{
    return m_writePos-m_readPos;
}

//前面可以用的空间
size_t Buffer::PrependableBytes() const
{
    return m_readPos;
}

//指向当前读的位置
const char *Buffer::Peek() const
{
    return M_BeginPtr() + m_readPos;
}

void Buffer::EnsureWriteable(size_t len)
{
    if(WritableBytes()<len){
        M_MakeSpace(len);
    }

    assert(WritableBytes()>=len);
}

void Buffer::HasWritten(size_t len)
{
    m_writePos += len;
}

void Buffer::Retrieve(size_t len)
{
    assert(len <= ReadableBytes());
    m_readPos += len;
}

void Buffer::RetrieveUntil(const char *end)
{
    assert(Peek() <= end );
    Retrieve(end - Peek());
}

void Buffer::RetrieveAll()
{
    bzero(&m_buffer[0], m_buffer.size());
    m_readPos = 0;
    m_writePos = 0;
}

std::string Buffer::RetrieveAllToStr()
{
    std::string str(Peek(), ReadableBytes());
    RetrieveAll();
    return str;
}

const char *Buffer::BeginWriteConst() const
{
    return M_BeginPtr() + m_writePos;
}

char *Buffer::BeginWrite()
{
    return M_BeginPtr() + m_writePos;
}

void Buffer::Append(const std::string &str)
{
    Append(str.data(), str.length());
}

void Buffer::Append(const char *str, size_t len)
{
    assert(str);
    EnsureWriteable(len);       //确保有可写的空间
    std::copy(str, str + len, BeginWrite());
    HasWritten(len);
}

void Buffer::Append(const void *data, size_t len)
{
    assert(data);
    Append(static_cast<const char*>(data), len);
}

void Buffer::Append(const Buffer &buff)
{
    Append(buff.Peek(), buff.ReadableBytes());
}

ssize_t Buffer::ReadFd(int fd, int *saveErrno)
{
    char buff[65535];       //临时数组，保证能够把所有的数据都读出来（65k
    struct iovec iov[2];
    const size_t writable = WritableBytes();
    /* 分散读， 保证数据全部读完 */
    iov[0].iov_base = M_BeginPtr() + m_writePos;
    iov[0].iov_len = writable;
    iov[1].iov_base = buff;
    iov[1].iov_len = sizeof(buff);

    const ssize_t len = readv(fd, iov, 2);
    if(len < 0) {
        *saveErrno = errno;
    }
    else if(static_cast<size_t>(len) <= writable) {
        m_writePos += len;
    }
    else {
        m_writePos = m_buffer.size();   //说明自己的已经满了，得存到临时的里面
        Append(buff, len - writable);   //buff临时数组，len-writeable追加到临时数组中的数据个数
    }
    return len;
}

ssize_t Buffer::WriteFd(int fd, int *saveErrno)
{
    size_t readSize = ReadableBytes();
    ssize_t len = write(fd, Peek(), readSize);
    if(len < 0) {
        *saveErrno = errno;
        return len;
    }
    m_readPos += len;
    return len;
}

char *Buffer::M_BeginPtr()
{
    return &*m_buffer.begin();
}

const char *Buffer::M_BeginPtr() const
{
    return &*m_buffer.begin();
}

void Buffer::M_MakeSpace(size_t len)
{
    if(WritableBytes() + PrependableBytes() < len) {        //不够装
        m_buffer.resize(m_writePos + len + 1);
    }
    else {      //加上前面已经读取完的空间 够装
        size_t readable = ReadableBytes();
        //M_BeginPtr() + m_readPos 输入范围的起始位置
        //M_BeginPtr() + m_writePos 输入范围的结束位置（不包含在复制范围内
        //M_BeginPtr() 指向输出位置 ，即复制后的数据将被写入的目标位置
        std::copy(M_BeginPtr() + m_readPos, M_BeginPtr() + m_writePos, M_BeginPtr());
        m_readPos = 0;
        m_writePos = m_readPos + readable;
        assert(readable == ReadableBytes());
    }
}
