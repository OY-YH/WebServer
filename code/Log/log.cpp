#include "log.h"

using namespace std;

void Log::init(int level=1, const char *path, const char *suffix, int maxQueueCapacity)
{
    m_isOpen = true;
    m_level = level;
    if(maxQueueCapacity > 0) {      //异步
        m_isAsync = true;
        if(!m_deque) {              //队列不存在
            unique_ptr<BlockDeque<std::string>> newDeque(new BlockDeque<std::string>);
            m_deque = move(newDeque);

            std::unique_ptr<std::thread> NewThread(new thread(FlushLogThread));
            m_writeThread = move(NewThread);
        }
    } else {
        m_isAsync = false;
    }

    m_lineCount = 0;

    //获取当前时间
    time_t timer = time(nullptr);
    struct tm *sysTime = localtime(&timer);
    struct tm t = *sysTime;
    m_path = path;
    m_suffix = suffix;      //.log
    char fileName[LOG_NAME_LEN] = {0};
    snprintf(fileName, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s",
             m_path, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, m_suffix);
    m_toDay = t.tm_mday;

    {
        lock_guard<mutex> locker(m_mtx);
        m_buff.RetrieveAll();
        if(m_fp) {      //在操作之前刷新进去，再把他关闭掉
            flush();
            fclose(m_fp);
        }

        m_fp = fopen(fileName, "a");        //新的
        if(m_fp == nullptr) {
            mkdir(m_path, 0777);
            m_fp = fopen(fileName, "a");
        }
        assert(m_fp != nullptr);
    }
}

Log *Log::Instance()
{
    static Log inst;
    return &inst;
}

void Log::FlushLogThread()      //子线程要做的事情
{
    Log::Instance()->M_AsyncWrite();    //异步写
}

void Log::write(int level, const char *format,...)           // 日志输出函数
{
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t tSec = now.tv_sec;
    struct tm *sysTime = localtime(&tSec);
    struct tm t = *sysTime;
    va_list vaList;

    /* 日志日期 日志行数 */
    if (m_toDay != t.tm_mday || (m_lineCount && (m_lineCount  %  MAX_LINES == 0)))
    {
        unique_lock<mutex> locker(m_mtx);
        locker.unlock();

        char newFile[LOG_NAME_LEN];
        char tail[36] = {0};
        snprintf(tail, 36, "%04d_%02d_%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

        if (m_toDay != t.tm_mday)
        {
            snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s%s", m_path, tail, m_suffix);
            m_toDay = t.tm_mday;
            m_lineCount = 0;
        }
        else {
            snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s-%d%s", m_path, tail, (m_lineCount  / MAX_LINES), m_suffix);
        }

        locker.lock();
        flush();
        fclose(m_fp);
        m_fp = fopen(newFile, "a");     //重新打开
        assert(m_fp != nullptr);
    }

    //具体的写数据
    {
        unique_lock<mutex> locker(m_mtx);
        m_lineCount++;
        int n = snprintf(m_buff.BeginWrite(), 128, "%d-%02d-%02d %02d:%02d:%02d.%06ld ",
                         t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                         t.tm_hour, t.tm_min, t.tm_sec, now.tv_usec);

        m_buff.HasWritten(n);
        M_AppendLogLevelTitle(level);

        va_start(vaList, format);
        int m = vsnprintf(m_buff.BeginWrite(), m_buff.WritableBytes(), format, vaList);     // 赋值 format 格式的 arg 到 m_buf
        va_end(vaList);

        m_buff.HasWritten(m);
        m_buff.Append("\n\0", 2);

        if(m_isAsync && m_deque && !m_deque->full()) {
            m_deque->push_back(m_buff.RetrieveAllToStr());
        } else {
            fputs(m_buff.Peek(), m_fp);
        }
        m_buff.RetrieveAll();
    }
}

void Log::flush()
{
    if(m_isAsync) {
        m_deque->flush();
    }
    fflush(m_fp);
}

int Log::GetLevel()
{
    lock_guard<mutex> locker(m_mtx);
    return m_level;
}

void Log::SetLevel(int level)
{
    lock_guard<mutex> locker(m_mtx);
    m_level = level;
}

Log::Log()
{
    m_lineCount = 0;
    m_isAsync = false;
    m_writeThread = nullptr;
    m_deque = nullptr;
    m_toDay = 0;
    m_fp = nullptr;
}

Log::~Log()
{
    if(m_writeThread && m_writeThread->joinable()) {
        while(!m_deque->empty()) {
            m_deque->flush();
        };
        m_deque->Close();
        m_writeThread->join();
    }
    if(m_fp) {
        lock_guard<mutex> locker(m_mtx);
        flush();
        fclose(m_fp);
    }
}

void Log::M_AppendLogLevelTitle(int level)      // 得到当前输入等级level的字符串
{
    switch(level) {
    case 0:
        m_buff.Append("[debug]: ", 9);
        break;
    case 1:
        m_buff.Append("[info] : ", 9);
        break;
    case 2:
        m_buff.Append("[warn] : ", 9);
        break;
    case 3:
        m_buff.Append("[error]: ", 9);
        break;
    default:
        m_buff.Append("[info] : ", 9);
        break;
    }
}

void Log::M_AsyncWrite()        //异步写
{
    string str = "";
    while(m_deque->pop(str)) {
        lock_guard<mutex> locker(m_mtx);
        fputs(str.c_str(), m_fp);
    }
}
