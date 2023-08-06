#ifndef BUFFER_H
#define BUFFER_H

#include <cstring>      //perror
#include <iostream>
#include <unistd.h>     // write
#include <sys/uio.h>    //readv
#include <vector>       //readv
#include <atomic>       //原子操作
#include <assert.h>

//自动增长的缓冲区，容器
class Buffer
{
public:
    Buffer(int initBuffSize = 1024);
    ~Buffer() = default;

    size_t WritableBytes() const;       //可写的字节数大小
    size_t ReadableBytes() const ;      //可读的字节数大小
    size_t PrependableBytes() const;    //可拓展的字节数

    const char* Peek() const;
    void EnsureWriteable(size_t len);
    void HasWritten(size_t len);

    void Retrieve(size_t len);      //取了len长度的了
    void RetrieveUntil(const char* end);    //取到end那

    void RetrieveAll() ;        //取回全部，就是移到最前面
    std::string RetrieveAllToStr();

    const char* BeginWriteConst() const;
    char* BeginWrite();

    void Append(const std::string& str);
    void Append(const char* str, size_t len);
    void Append(const void* data, size_t len);
    void Append(const Buffer& buff);

    ssize_t ReadFd(int fd, int* saveErrno);
    ssize_t WriteFd(int fd, int* saveErrno);

private:
    char* M_BeginPtr();
    const char* M_BeginPtr() const;
    void M_MakeSpace(size_t len);       //创建新的空间

private:
    std::vector<char> m_buffer;             //具体装数据的vector
    std::atomic<std::size_t> m_readPos;     //读的位置
    std::atomic<std::size_t> m_writePos;    //写的位置
};

#endif // BUFFER_H
