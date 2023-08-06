#include <iostream>

#include <cstdio>
#include <cstdlib>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <signal.h>
#include <assert.h>     //断言函数

#include "server/webserver.h"

int main()
{
    /* 守护进程 后台运行 */
    //daemon(1, 0);

    WebServer server(
        1316, 3, 60000, false,             /* 端口 ET模式 timeoutMs 优雅退出  */
        3306, "root", "oyyh02111208", "webserver", /* Mysql配置 端口号 用户名 用户密码 数据库名 */
        12, 6, true, 1, 1024               /* 数据库连接池数量 线程池数量 日志开关 日志等级 日志异步队列容量 */
    );

    server.Start();



}

