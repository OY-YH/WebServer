#ifndef SQLCONNPOOL_H
#define SQLCONNPOOL_H

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <semaphore.h>
#include <thread>
#include <assert.h>

#include "Log/log.h"

class SqlConnPool
{
public:
    static SqlConnPool *Instance();     //静态化一个实例，单例模式



    void Init(const char* host, int port,
              const char* user,const char* pwd,
              const char* dbName, int connSize);

    MYSQL *GetConn();                   //获取一个连接
    void FreeConn(MYSQL * sql);        //释放一个数据库连接（并不是真正释放，而是又放到池子里
    int GetFreeConnCount();             //获取空闲数量
    void ClosePool();                   //关闭池子

private:
    SqlConnPool();
    ~SqlConnPool();

private:
    int m_MAX_CONN;     //最大的连接数
    int m_useCount;     //当前的用户数
    int m_freeCount;    //空闲的用户数

    std::queue<MYSQL *> m_connQue;      //队列（MYSQL*)
    std::mutex m_mtx;                   //互斥锁
    sem_t m_semId;                      //信号量
};

#endif // SQLCONNPOOL_H
