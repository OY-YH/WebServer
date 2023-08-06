#ifndef SQLCONNRAII_H
#define SQLCONNRAII_H

#include "sqlconnpool.h"

/* 资源在对象构造初始化 资源在对象析构时释放*/
class SqlConnRAII {
public:
    SqlConnRAII(MYSQL** sql, SqlConnPool *connpool) {
        assert(connpool);

        *sql = connpool->GetConn();
        m_sql = *sql;
        m_connpool = connpool;
    }

    ~SqlConnRAII() {
        if(m_sql) { m_connpool->FreeConn(m_sql); }
    }

private:
    MYSQL *m_sql;
    SqlConnPool* m_connpool;
};

#endif // SQLCONNRAII_H
