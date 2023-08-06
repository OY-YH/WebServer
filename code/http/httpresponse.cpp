#include "httpresponse.h"

using namespace  std;

const std::unordered_map<std::string, std::string> HttpResponse::SUFFIX_TYPE = {
    { ".html",  "text/html" },
    { ".xml",   "text/xml" },
    { ".xhtml", "application/xhtml+xml" },
    { ".txt",   "text/plain" },
    { ".rtf",   "application/rtf" },
    { ".pdf",   "application/pdf" },
    { ".word",  "application/nsword" },
    { ".png",   "image/png" },
    { ".gif",   "image/gif" },
    { ".jpg",   "image/jpeg" },
    { ".jpeg",  "image/jpeg" },
    { ".au",    "audio/basic" },
    { ".mpeg",  "video/mpeg" },
    { ".mpg",   "video/mpeg" },
    { ".avi",   "video/x-msvideo" },
    { ".gz",    "application/x-gzip" },
    { ".tar",   "application/x-tar" },
    { ".css",   "text/css "},
    { ".js",    "text/javascript "},
    };

const std::unordered_map<int, std::string> HttpResponse::CODE_STATUS = {
    { 200, "OK" },
    { 400, "Bad Request" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
    };

const std::unordered_map<int, std::string> HttpResponse::CODE_PATH = {
    { 400, "/400.html" },
    { 403, "/403.html" },
    { 404, "/404.html" },
    };

HttpResponse::HttpResponse()
{
    m_code = -1;
    m_path = m_srcDir = "";
    m_isKeepAlive = false;
    m_mmFile = nullptr;
    m_mmFileStat = { 0 };
}

HttpResponse::~HttpResponse()
{
    UnmapFile();
}

void HttpResponse::Init(const std::string &srcDir, std::string &path, bool isKeepAlive, int code)
{
    assert(srcDir != "");
    if(m_mmFile) {
        UnmapFile();    //释放内存映射文件
    }
    m_code = code;
    m_isKeepAlive = isKeepAlive;
    m_path = path;
    m_srcDir = srcDir;
    m_mmFile = nullptr;
    m_mmFileStat = { 0 };
}

void HttpResponse::MakeResponse(Buffer &buff)
{
    /* 判断请求的资源文件 */
    //index.html
    // /run/media/root/study/C++work/webserver/resources/index.html
    if(stat((m_srcDir + m_path).data(), &m_mmFileStat) < 0 || S_ISDIR(m_mmFileStat.st_mode)) {  //判断是否为目录
        m_code = 404;
    }
    else if(!(m_mmFileStat.st_mode & S_IROTH)) {    //判断权限
        m_code = 403;
    }
    else if(m_code == -1) {     //默认
        m_code = 200;
    }
    M_ErrorHtml();      //寻找错误网页
    M_AddStateLine(buff);   //添加响应首行
    M_AddHeader(buff);
    M_AddContent(buff);
}

void HttpResponse::UnmapFile()
{
    if(m_mmFile) {
        munmap(m_mmFile, m_mmFileStat.st_size);
        m_mmFile = nullptr;
    }
}

char *HttpResponse::File()
{
    return m_mmFile;
}

size_t HttpResponse::FileLen() const
{
    return m_mmFileStat.st_size;
}

void HttpResponse::ErrorContent(Buffer &buff, std::string message)
{
    string body;
    string status;
    body += "<html><title>Error</title>";
    body += "<body bgcolor=\"ffffff\">";
    if(CODE_STATUS.count(m_code) == 1) {
        status = CODE_STATUS.find(m_code)->second;
    } else {
        status = "Bad Request";
    }
    body += to_string(m_code) + " : " + status  + "\n";
    body += "<p>" + message + "</p>";
    body += "<hr><em>TinyWebServer</em></body></html>";

    buff.Append("Content-length: " + to_string(body.size()) + "\r\n\r\n");
    buff.Append(body);
}

void HttpResponse::M_AddStateLine(Buffer &buff)
{
    std::string status;
    if(CODE_STATUS.count(m_code) == 1) {
        status = CODE_STATUS.find(m_code)->second;
    }
    else {
        m_code = 400;
        status = CODE_STATUS.find(400)->second;
    }

    //http/1.1 200 ok
    buff.Append("HTTP/1.1 " + std::to_string(m_code) + " " + status + "\r\n");
}

void HttpResponse::M_AddHeader(Buffer &buff)
{
    buff.Append("Connection: ");
    if(m_isKeepAlive) {
        buff.Append("keep-alive\r\n");
        buff.Append("keep-alive: max=6, timeout=120\r\n");
    } else{
        buff.Append("close\r\n");
    }
    buff.Append("Content-type: " + M_GetFileType() + "\r\n");
}

void HttpResponse::M_AddContent(Buffer &buff)
{
    int srcFd = open((m_srcDir + m_path).data(), O_RDONLY);
    if(srcFd < 0) {
        ErrorContent(buff, "File NotFound!");
        return;
    }

    /* 将文件映射到内存提高文件的访问速度
        MAP_PRIVATE 建立一个写入时拷贝的私有映射*/

    //响应正文在内存里（文件
    LOG_DEBUG("file path %s", (m_srcDir + m_path).data());
    int* mmRet = (int*)mmap(0, m_mmFileStat.st_size, PROT_READ, MAP_PRIVATE, srcFd, 0);
    if(*mmRet == -1) {
        ErrorContent(buff, "File NotFound!");
        return;
    }
    m_mmFile = (char*)mmRet;
    close(srcFd);
    //这个是存其他的响应报文
    buff.Append("Content-length: " + to_string(m_mmFileStat.st_size) + "\r\n\r\n");
}

void HttpResponse::M_ErrorHtml()
{
    if(CODE_PATH.count(m_code) == 1) {
        m_path = CODE_PATH.find(m_code)->second;
        stat((m_srcDir + m_path).data(), &m_mmFileStat);
    }
}

std::string HttpResponse::M_GetFileType()
{
    /* 判断文件类型 */
    string::size_type idx = m_path.find_last_of('.');
    if(idx == string::npos) {
        return "text/plain";
    }
    //获取后缀
    string suffix = m_path.substr(idx);
    if(SUFFIX_TYPE.count(suffix) == 1) {
        return SUFFIX_TYPE.find(suffix)->second;
    }
    return "text/plain";
}

