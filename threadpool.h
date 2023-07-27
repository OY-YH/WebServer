#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include <cstdio>

#include "locker.h"

//由于任务的类型 采用模板的方式
//线程池类，定义成模板类是为了代码的复用(可能在别的项目中任务又是另一种类型
//模板参数T就是任务类
template<typename T>
class threadpool
{
public:
    threadpool(int thread_number=8,int max_requests=10000);
    ~threadpool();

    //主线程往队列中添加任务
    bool append(T* request);

private:
    //c++的类成员函数都有一个默认参数this指针，而线程调用的时候，限制了只能有一个参数void* arg,如果不设置静态在调用的时候会出现this和arg都给worker,而导致错误
    //所以用static就不默认加this
    static void* worker(void* arg);
    //启动线程池，从工作队列中去数据，去做任务
    void run();
private:
    //线程的数量
    int m_thread_number;

    //线程池数组，大小为m_thread_number
    pthread_t* m_threads;

    //请求队列最多允许的，等待处理的请求数量
    int m_max_requests;

    //请求队列 大部分操作都是插入删除操作，用链表更快
    std::list<T*> m_workqueue;

    //互斥锁
    //保护请求队列，因为线程们包括主线程共享这个请求队列，从这个队列取任务，所以要线程同步，保证线程的独占式访问
    locker m_queuelocker;

    //信号量用来判断是否有任务需要处理（队列中剩余的任务数
    //为了阻塞线程，没有信号量线程就的一直循环判断队列中有没有任务，造成cpu空转
    sem m_queuestat;

    //是否结束线程
    bool m_stop;

};

//模板定义声明最好在一个文件里
template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests)
    :m_thread_number(thread_number),m_max_requests(max_requests),
    m_stop(false),m_threads(nullptr)
{
    if(thread_number<=0||max_requests<=0){
        throw std::exception();
    }

    m_threads=new pthread_t[m_thread_number];
    if(!m_threads){
        throw std::exception();
    }

    //创建thread_number个线程，并将他们设置为线程脱离
    for(int i=0;i<thread_number;++i){
        printf("create the %dth thread\n",i);

        //worker必须是静态函数
        //静态函数不能访问非静态成员等，可以通过参数this传递参数进来，this是threadpool类型
        if( pthread_create(m_threads+i,NULL,worker,this)!=0){
            delete [] m_threads;
            throw std::exception();
        }

        if(pthread_detach(m_threads[i])){
            delete [] m_threads;
            throw std::exception();
        }

    }
}

template<typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
    m_stop=true;
}

//主线程添加请求队列
template<typename T>
bool threadpool<T>::append(T *request)
{
    //主线程添加请求队列，此时其他线程不能操作队列
    m_queuelocker.lock();
    if(m_workqueue.size() >= m_max_requests){
        m_queuelocker.unlock();
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); //通知子线程来任务了

    return true;
}

template<typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool * pool=(threadpool *)arg;
    pool->run();
    return pool;
}

//子线程操作请求队列，并工作
template<typename T>
void threadpool<T>::run()
{
    while(!m_stop){
        //从工作队列中取一个，然后去做任务

        //如果信号量有值，不阻塞，信号量值减1
        //如果没有值，阻塞
        m_queuestat.wait();

        //有值，上锁
        //获取任务时，要使用互斥锁，保证对资源的独占式访问
        m_queuelocker.lock();

        //信号量表示请求数量，请求数量为0,会阻塞在wait处，
        //所以下面这个判断可以去掉
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }

        T* request=m_workqueue.front(); //获取任务
        m_workqueue.pop_front();

        m_queuelocker.unlock();

        if(!request){
            continue;
        }

        //做任务
        //调用任务的工作 逻辑函数
        request->process();     //执行任务，这里不用锁，并发执行
    }
}




#endif // THREADPOOL_H

