#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <mutex>
#include <string>
#include <thread>
#include <sys/time.h>
#include <string.h>
#include <stdarg.h>           // vastart va_end
#include <assert.h>
#include <sys/stat.h>         //mkdir
#include "BlockQueue.h"
#include "buffer.h"

class Log {
public:
    void init(int level, const char* path = "./log",
              const char* suffix =".log",
              int maxQueueCapacity = 1024);

    static Log* Instance();         //单例

    static void FlushLogThread();

    void write(int level, const char *format,...);
    void flush();               //刷新

    int GetLevel();
    void SetLevel(int level);
    bool IsOpen() { return m_isOpen; }

private:
    Log();
    virtual ~Log();

    void M_AppendLogLevelTitle(int level);
    void M_AsyncWrite();

private:
    static const int LOG_PATH_LEN = 256;
    static const int LOG_NAME_LEN = 256;
    static const int MAX_LINES = 50000;

    const char* m_path;
    const char* m_suffix;   //后缀

    int m_MAX_LINES;        //最多的行数

    int m_lineCount;        //已写的行数
    int m_toDay;            //记录今天的日期

    bool m_isOpen;

    Buffer m_buff;          // 缓存（日志信息 文件）
    int m_level;            //设置的日志级别 声明当前程序的日志等级状态 只输出等级等于或高于该值的内容
    bool m_isAsync;         //是否异步

    FILE* m_fp;
    std::unique_ptr<BlockDeque<std::string>> m_deque;
    std::unique_ptr<std::thread> m_writeThread;     //写的子线程，异步要开启
    std::mutex m_mtx;
};

// 宏定义（宏函数），隐藏形参
#define LOG_BASE(level, format, ...) \
    do {\
        Log* log = Log::Instance();\
        if (log->IsOpen() && log->GetLevel() <= level) {\
            log->write(level, format, ##__VA_ARGS__); \
            log->flush();\
        }\
    } while(0);

// 日志等级，越往下等级越高
//##__VA_ARGS__ 获取多个参数
#define LOG_DEBUG(format, ...) do {LOG_BASE(0, format, ##__VA_ARGS__)} while(0);
#define LOG_INFO(format, ...) do {LOG_BASE(1, format, ##__VA_ARGS__)} while(0);
#define LOG_WARN(format, ...) do {LOG_BASE(2, format, ##__VA_ARGS__)} while(0);
#define LOG_ERROR(format, ...) do {LOG_BASE(3, format, ##__VA_ARGS__)} while(0);

#endif // LOG_H
