#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdio.h>

#include "locker.h"
#include "noactive/lst_timer.h"
#include "http_conn.h"

#define OPEN_LOG 1                  // 声明是否打开日志输出
#define LOG_LEVEL LOGLEVEL_DEBUG     // 声明当前程序的日志等级状态，只输出等级等于或高于该值的内容
#define LOG_SAVE 0                  // 可补充日志保存功能

typedef enum{                       // 日志等级，越往下等级越高
    LOGLEVEL_DEBUG = 0,
    LOGLEVEL_INFO,
    LOGLEVEL_WARN,
    LOGLEVEL_ERROR,
}E_LOGLEVEL;

char *EM_logLevelGet(const int level);

void EM_log(const int level, const char* fun, const int line, const char *fmt, ...);

#define EMlog(level, fmt...) EM_log(level, __FUNCTION__, __LINE__, fmt) // 宏定义，隐藏形参

#endif // LOG_H
