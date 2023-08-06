#include "sqlconnpool.h"

using namespace std;

SqlConnPool *SqlConnPool::Instance()
{
    static SqlConnPool connPool;        //静态化一个实例，单例模式
    return &connPool;
}

void SqlConnPool::Init(const char *host, int port, const char *user, const char *pwd, const char *dbName, int connSize = 10)
{
    assert(connSize > 0);
    for (int i = 0; i < connSize; i++) {
        MYSQL *sql = nullptr;
        sql = mysql_init(sql);      //初始化连接
        if (!sql) {
            LOG_ERROR("MySql init error!");
            assert(sql);
        }
        //连接到数据库
        sql = mysql_real_connect(sql, host,
                                 user, pwd,
                                 dbName, port, nullptr, 0);
        if (!sql) {
            LOG_ERROR("MySql Connect error!");
        }
        m_connQue.push(sql);
    }
    m_MAX_CONN = connSize;
    sem_init(&m_semId, 0, m_MAX_CONN);
}

MYSQL *SqlConnPool::GetConn()
{
    MYSQL *sql = nullptr;
    if(m_connQue.empty()){
        LOG_WARN("SqlConnPool busy!");
        return nullptr;
    }
    sem_wait(&m_semId);     //消费 -1
    {
        lock_guard<mutex> locker(m_mtx);
        sql = m_connQue.front();
        m_connQue.pop();
    }
    return sql;
}

void SqlConnPool::FreeConn(MYSQL *sql)
{
    assert(sql);
    lock_guard<mutex> locker(m_mtx);
    m_connQue.push(sql);
    sem_post(&m_semId);     //生产 +1
}

int SqlConnPool::GetFreeConnCount()
{
    lock_guard<mutex> locker(m_mtx);
    return m_connQue.size();
}

void SqlConnPool::ClosePool()
{
    lock_guard<mutex> locker(m_mtx);
    while(!m_connQue.empty()) {
        auto item = m_connQue.front();
        m_connQue.pop();
        mysql_close(item);
    }
    mysql_library_end();
}

SqlConnPool::SqlConnPool()
{
    m_useCount = 0;
    m_freeCount = 0;
}

SqlConnPool::~SqlConnPool()
{
    ClosePool();
}

