#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

// 线程同步机制封装类

// 互斥锁类
class locker
{
public:
    locker();
    ~locker();
    //上锁
    bool lock();
    //解锁
    bool unlock() ;

    pthread_mutex_t *get();

private:
    pthread_mutex_t m_mutex;
};

// 条件变量类
class cond {
public:
    cond();
    ~cond();

    bool wait(pthread_mutex_t *m_mutex);
    bool timedwait(pthread_mutex_t *m_mutex, struct timespec t) ;
    //增加条件变量（让一个或者多个线程唤醒
    bool signal();
    //唤醒全部线程
    bool broadcast();

private:
    pthread_cond_t m_cond;
};


// 信号量类
class sem {
public:
    sem();
    sem(int num);
    ~sem() ;

    // 等待信号量（消费）
    bool wait();
    // 增加信号量（生产）
    bool post();
private:
    sem_t m_sem;
};
#endif // LOCKER_H
