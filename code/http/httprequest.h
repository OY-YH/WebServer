#ifndef HTTPREQUEST_H
#define HTTPREQUEST_H

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <regex>
#include <errno.h>
#include <mysql/mysql.h>  //mysql

#include "buffer.h"
#include "pool/SqlConnRAII.h"
#include "Log/log.h"

class HttpRequest
{
public:
    enum PARSE_STATE {
        REQUEST_LINE,       //正在解析请求首行
        HEADERS,            //头
        BODY,               //体
        FINISH,             //解析完成
    };

    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完整的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */

    enum HTTP_CODE {
        NO_REQUEST = 0,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURSE,
        FORBIDDENT_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION,
    };

public:
    HttpRequest(){
        Init();
    }
    ~HttpRequest() = default;

    void Init();
    bool parse(Buffer& buff);

    std::string path() const;
    std::string& path();
    std::string method() const;
    std::string version() const;
    std::string GetPost(const std::string& key) const;
    std::string GetPost(const char* key) const;

    bool IsKeepAlive() const;

    /*
    todo
    void HttpConn::ParseFormData() {}
    void HttpConn::ParseJson() {}
    */

private:
    //解析http请求行，获得请求方法，目标URL,HTTP版本
    bool M_ParseRequestLine(const std::string& line);
    //解析http请求头
    void M_ParseHeader(const std::string& line);
    //解析http请求体
    void M_ParseBody(const std::string& line);

    void M_ParsePath();                 //解析路径
    void M_ParsePost();                 //解析post
    void M_ParseFromUrlencoded();       //解析表单数据

    //验证用户登陆
    static bool UserVerify(const std::string& name, const std::string& pwd, bool isLogin);

    static int ConverHex(char ch);      //转换成十六进制
private:
    PARSE_STATE m_state;        //解析的状态
    std::string m_method, m_path, m_version, m_body;   //请求方法，请求路径，协议版本
    std::unordered_map<std::string, std::string> m_header;  //请求头
    std::unordered_map<std::string, std::string> m_post;     //post请求表单数据

    static const std::unordered_set<std::string> DEFAULT_HTML;  //默认的网页
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG;
};

#endif // HTTPREQUEST_H
