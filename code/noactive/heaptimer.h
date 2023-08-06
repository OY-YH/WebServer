#ifndef HEAPTIMER_H
#define HEAPTIMER_H

#include <queue>
#include <unordered_map>
#include <time.h>
#include <algorithm>
#include <arpa/inet.h>
#include <functional>
#include <assert.h>
#include <chrono>

typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::high_resolution_clock Clock;
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;

struct TimerNode {
    int id;                 //fd文件描述符
    TimeStamp expires;      //超时时间
    TimeoutCallBack cb;     // 任务回调函数，回调函数处理的客户数据，由定时器的执行者传递给回调函数
    bool operator<(const TimerNode& t) {    //运算符<重载
        return expires < t.expires;
    }
};

class HeapTimer
{
public:
    HeapTimer();

    ~HeapTimer() { clear(); }

    void adjust(int id, int timeout);        //调整节点

    void add(int id, int timeOut, const TimeoutCallBack& cb);   //添加节点

    void doWork(int id);

    void clear();

    void tick();                        /* 清除超时结点 */

    void pop();

    int GetNextTick();                  //获取下一个要清除的点

private:
    void M_del(size_t index);

    void M_siftup(size_t i);                    //向上调整

    bool M_siftdown(size_t index, size_t n);    //向下调整

    void M_SwapNode(size_t i, size_t j);        //交换节点

    std::vector<TimerNode> m_heap;

    std::unordered_map<int, size_t> m_ref;      //<fd,索引>
};

#endif // HEAPTIMER_H
