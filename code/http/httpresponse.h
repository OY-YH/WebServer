#ifndef HTTPRESPONSE_H
#define HTTPRESPONSE_H

#include <unordered_map>
#include <fcntl.h>       // open
#include <unistd.h>      // close
#include <sys/stat.h>    // stat
#include <sys/mman.h>    // mmap, munmap

#include "buffer.h"
#include "Log/log.h"


class HttpResponse
{
public:
    HttpResponse();
    ~HttpResponse();

    void Init(const std::string& srcDir, std::string& path, bool isKeepAlive = false, int code = -1);
    void MakeResponse(Buffer& buff);        //生成响应

    void UnmapFile();                       //解除内存映射
    char* File();
    size_t FileLen() const;

    void ErrorContent(Buffer& buff, std::string message);
    int Code() const { return m_code; }

private:
    void M_AddStateLine(Buffer &buff);
    void M_AddHeader(Buffer &buff);
    void M_AddContent(Buffer &buff);

    void M_ErrorHtml();
    std::string M_GetFileType();
private:
    int m_code;             //响应状态码
    bool m_isKeepAlive;     //是否保持连接

    std::string m_path;     //资源路径
    std::string m_srcDir;   //资源目录

    //内存映射的文件
    char* m_mmFile;             //文件内存映射的指针
    struct stat m_mmFileStat;   //文件的状态信息

    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE;  //后缀-类型
    static const std::unordered_map<int, std::string> CODE_STATUS;  //状态码-描述
    static const std::unordered_map<int, std::string> CODE_PATH;    //状态码-路径
};

#endif // HTTPRESPONSE_H
