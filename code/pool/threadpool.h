#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <functional>

class ThreadPool
{
public:
    explicit ThreadPool(size_t threadCount = 8): m_pool(std::make_shared<Pool>()) {
        assert(threadCount > 0);

        for(size_t i = 0; i < threadCount; i++) {
            std::thread([pool = m_pool] {
                std::unique_lock<std::mutex> locker(pool->mtx);
                while(true) {
                    if(!pool->tasks.empty()) {      //从任务队列中取任务
                        auto task = std::move(pool->tasks.front());
                        //移除掉
                        pool->tasks.pop();

                        locker.unlock();
                        task();
                        locker.lock();
                    }
                    else if(pool->isClosed) break;      //没有，关闭
                    else pool->cond.wait(locker);       //阻塞，等待
                }
            }).detach();        //线程分析
        }
    }

    ThreadPool() = default;

    ThreadPool(ThreadPool&&) = default;

    ~ThreadPool() {
        if(static_cast<bool>(m_pool)) {
            {
                std::lock_guard<std::mutex> locker(m_pool->mtx);
                m_pool->isClosed = true;
            }
            m_pool->cond.notify_all();      //唤醒所有线程
        }
    }

    template<class F>
    void AddTask(F&& task) {
        {
            std::lock_guard<std::mutex> locker(m_pool->mtx);
            m_pool->tasks.emplace(std::forward<F>(task));    //拷贝到容器中
        }
        m_pool->cond.notify_one();       //唤醒一个线程
    }

private:
    struct Pool {
        std::mutex mtx;                             //互斥锁
        std::condition_variable cond;               //条件变量
        bool isClosed;                              //是否关闭
        std::queue<std::function<void()>> tasks;    //队列（保存的是任务）
    };
    std::shared_ptr<Pool> m_pool;                   //池子
};

#endif // THREADPOOL_H
