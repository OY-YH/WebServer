#include "httprequest.h"

using namespace std;

const std::unordered_set<std::string> HttpRequest::DEFAULT_HTML{
    "/index", "/register", "/login",
    "/welcome", "/video", "/picture", };

const std::unordered_map<std::string, int> HttpRequest::DEFAULT_HTML_TAG {
    {"/register.html", 0}, {"/login.html", 1},  };


void HttpRequest::Init()
{
    m_method = m_path = m_version = m_body = "";
    m_state = REQUEST_LINE;
    m_header.clear();
    m_post.clear();
}

//主状态机
bool HttpRequest::parse(Buffer &buff)
{
    const char CRLF[] = "\r\n";
    if(buff.ReadableBytes() <= 0) {
        return false;
    }
    // 主状态机正在解析请求，且从状态机OK，不需要一行一行解析
    while(buff.ReadableBytes() && m_state != FINISH) {
        //读取一行数据，根据\r\n为结束标志
        const char* lineEnd = search(buff.Peek(), buff.BeginWriteConst(), CRLF, CRLF + 2);      //查找第一个\r\n的位置
        //解析到了一行完整的数据，或者解析到了请求体，也是完整的数据
        std::string line(buff.Peek(), lineEnd);
        switch(m_state)
        {
        case REQUEST_LINE:
            if(!M_ParseRequestLine(line)) {
                return false;
            }
            M_ParsePath();
            break;
        case HEADERS:
            M_ParseHeader(line);
            if(buff.ReadableBytes() <= 2) {
                m_state = FINISH;
            }
            break;
        case BODY:
            M_ParseBody(line);
            break;
        default:
            break;
        }
        if(lineEnd == buff.BeginWrite()) {      //解析完
            break;
        }
        buff.RetrieveUntil(lineEnd + 2);    //没解析完，往后移,再读取到lineend+2那里
    }
    LOG_DEBUG("[%s], [%s], [%s]", m_method.c_str(), m_path.c_str(), m_version.c_str());
    return true;
}

string HttpRequest::path() const
{
    return m_path;
}

string &HttpRequest::path()
{
    return m_path;
}

bool HttpRequest::IsKeepAlive() const
{
    if(m_header.count("Connection") == 1) {
        return m_header.find("Connection")->second == "keep-alive" && m_version == "1.1";
    }
    return false;
}

bool HttpRequest::M_ParseRequestLine(const std::string &line)
{
    //   GET / HTTP/1.1
    //没有考虑错误的情况
    regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    smatch subMatch;
    if(regex_match(line, subMatch, patten)) {
        m_method = subMatch[1];
        m_path = subMatch[2];
        m_version = subMatch[3];
        m_state = HEADERS;
        return true;
    }
    LOG_ERROR("RequestLine Error");
    return false;
}

//Accept: text/html application/xhtml+xml application/xml;
void HttpRequest::M_ParseHeader(const std::string &line)
{
    regex patten("^([^:]*): ?(.*)$");
    smatch subMatch;
    if(regex_match(line, subMatch, patten)) {
        m_header[subMatch[1]] = subMatch[2];
    }
    else {  //解析到回车换行的时候，不匹配
        m_state = BODY;
    }
}

void HttpRequest::M_ParseBody(const std::string &line)
{
    m_body = line;
    M_ParsePost();
    m_state = FINISH;
    LOG_DEBUG("Body:%s, len:%d", line.c_str(), line.size());
}

void HttpRequest::M_ParsePath()
{
    if(m_path == "/") {
        m_path = "/index.html";
    }
    else {
        for(auto &item: DEFAULT_HTML) {
            if(item == m_path) {
                m_path += ".html";
                break;
            }
        }
    }
}

void HttpRequest::M_ParsePost()
{
    if(m_method == "POST" && m_header["Content-Type"] == "application/x-www-form-urlencoded") {
        //解析表单信息
        M_ParseFromUrlencoded();
        if(DEFAULT_HTML_TAG.count(m_path)) {
            int tag = DEFAULT_HTML_TAG.find(m_path)->second;
            LOG_DEBUG("Tag:%d", tag);
            if(tag == 0 || tag == 1) {
                bool isLogin = (tag == 1);
                if(UserVerify(m_post["username"], m_post["password"], isLogin)) {
                    m_path = "/welcome.html";
                }
                else {
                    m_path = "/error.html";
                }
            }
        }
    }
}

void HttpRequest::M_ParseFromUrlencoded()
{
    if(m_body.size() == 0) { return; }

    string key, value;
    int num = 0;
    int n = m_body.size();
    int i = 0, j = 0;

    //username=hello&password=hello
    for(; i < n; i++) {
        char ch = m_body[i];
        switch (ch) {
        case '=':
            key = m_body.substr(j, i - j);
            j = i + 1;
            break;
        case '+':
            m_body[i] = ' ';
            break;
        case '%':
            //简单的加密操作，编码 如果是中文，那个请求体的表单数据会变成十六进制的数据 ，数据库里的数据不会和这个一样，因为进行了加密
            num = ConverHex(m_body[i + 1]) * 16 + ConverHex(m_body[i + 2]);
            m_body[i + 2] = num % 10 + '0';
            m_body[i + 1] = num / 10 + '0';
            i += 2;
            break;
        case '&':
            value = m_body.substr(j, i - j);
            j = i + 1;
            m_post[key] = value;
            LOG_DEBUG("%s = %s", key.c_str(), value.c_str());
            break;
        default:
            break;
        }
    }
    assert(j <= i);
    if(m_post.count(key) == 0 && j < i) {
        value = m_body.substr(j, i - j);
        m_post[key] = value;
    }
}

bool HttpRequest::UserVerify(const std::string &name, const std::string &pwd, bool isLogin)
{
    if(name == "" || pwd == "") { return false; }
    LOG_INFO("Verify name:%s pwd:%s", name.c_str(), pwd.c_str());

    MYSQL* sql;
    SqlConnRAII(&sql,  SqlConnPool::Instance());
    assert(sql);

    bool flag = false;
    unsigned int j = 0;
    char order[256] = { 0 };
    MYSQL_FIELD *fields = nullptr;
    MYSQL_RES *res = nullptr;

    if(!isLogin) { flag = true; }   //未登录
    /* 查询用户及密码 */
    snprintf(order, 256, "SELECT username, password FROM user WHERE username='%s' LIMIT 1", name.c_str());
    LOG_DEBUG("%s", order);

    if(mysql_query(sql, order)) {
        mysql_free_result(res);         //释放结果
        return false;
    }
    res = mysql_store_result(sql);      //保存结果
    j = mysql_num_fields(res);          //获取字段
    fields = mysql_fetch_fields(res);   //提取字段

    while(MYSQL_ROW row = mysql_fetch_row(res)) {
        LOG_DEBUG("MYSQL ROW: %s %s", row[0], row[1]);
        string password(row[1]);
        /* 注册行为 且 用户名未被使用*/
        if(isLogin) {
            if(pwd == password) { flag = true; }
            else {
                flag = false;
                LOG_DEBUG("pwd error!");
            }
        }
        else {
            flag = false;
            LOG_DEBUG("user used!");
        }
    }
    mysql_free_result(res);

    /* 注册行为 且 用户名未被使用*/
    if(!isLogin && flag == true) {
        LOG_DEBUG("regirster!");
        bzero(order, 256);
        snprintf(order, 256,"INSERT INTO user(username, password) VALUES('%s','%s')", name.c_str(), pwd.c_str());
        LOG_DEBUG( "%s", order);
        if(mysql_query(sql, order)) {
            LOG_DEBUG( "Insert error!");
            flag = false;
        }
        flag = true;
    }
    SqlConnPool::Instance()->FreeConn(sql);
    LOG_DEBUG( "UserVerify success!!");
    return flag;
}

int HttpRequest::ConverHex(char ch)
{
    if(ch >= 'A' && ch <= 'F') return ch -'A' + 10;
    if(ch >= 'a' && ch <= 'f') return ch -'a' + 10;
    return ch;
}
