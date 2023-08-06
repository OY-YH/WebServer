#include "heaptimer.h"

HeapTimer::HeapTimer()
{

}

void HeapTimer::adjust(int id, int timeout)
{
    /* 调整指定id的结点 */
    assert(!m_heap.empty() && m_ref.count(id) > 0);
    m_heap[m_ref[id]].expires = Clock::now() + MS(timeout);     //新的超时时间
    M_siftdown(m_ref[id], m_heap.size());
}

void HeapTimer::add(int id, int timeout, const TimeoutCallBack &cb)
{
    assert(id >= 0);
    size_t i=0;
    if(m_ref.count(id) == 0) {
        /* 新节点：堆尾插入，调整堆 */
        i = m_heap.size();
        m_ref[id] = i;
        m_heap.push_back({id, Clock::now() + MS(timeout), cb});
        M_siftup(i);        //向上调整，跟父亲比较
    }
    else {
        /* 已有结点：调整堆 */
        i = m_ref[id];
        m_heap[i].expires = Clock::now() + MS(timeout);
        m_heap[i].cb = cb;
        if(!M_siftdown(i, m_heap.size())) {
            M_siftup(i);
        }
    }
}

void HeapTimer::doWork(int id)
{
    /* 删除指定id结点，并触发回调函数 */
    if(m_heap.empty() || m_ref.count(id) == 0) {
        return;
    }
    size_t i = m_ref[id];
    assert(i>=0 && i<m_heap.size());
    TimerNode node = m_heap[i];
    node.cb();
    M_del(i);
}

void HeapTimer::clear()
{
    m_ref.clear();
    m_heap.clear();
}

void HeapTimer::tick()
{
    /* 清除超时结点 */
    if(m_heap.empty()) {
        return;
    }
    while(!m_heap.empty()) {
        TimerNode node = m_heap.front();
        if(std::chrono::duration_cast<MS>(node.expires - Clock::now()).count() > 0) {   //还未超时
            break;
        }
        node.cb();      //执行回调函数
        pop();
    }
}

void HeapTimer::pop()
{
    assert(!m_heap.empty());
    M_del(0);
}

int HeapTimer::GetNextTick()
{
    tick();
    size_t res = -1;
    if(!m_heap.empty()) {
        res = std::chrono::duration_cast<MS>(m_heap.front().expires - Clock::now()).count();
        if(res < 0) { res = 0; }    //超时
    }
    return res;
}

void HeapTimer::M_del(size_t index)
{
    /* 删除指定位置的结点 */
    assert(!m_heap.empty() && index >= 0 && index < m_heap.size());
    /* 将要删除的结点换到队尾，然后调整堆 */
    size_t i = index;
    size_t n = m_heap.size() - 1;
    assert(i <= n);
    if(i < n) {
        M_SwapNode(i, n);
        if(!M_siftdown(i, n)) {
            M_siftup(i);
        }
    }
    /* 队尾元素删除 */
    m_ref.erase(m_heap.back().id);
    m_heap.pop_back();
}

void HeapTimer::M_siftup(size_t i)
{
    assert(i >= 0 && i < m_heap.size());
    if(i==0){
        return ;        //已经是根节点，无需向上调整
    }
    size_t j = (i - 1) / 2;     //父亲索引
//    while(j >= 0) {
    while(j<m_heap.size()){
        if(m_heap[j] < m_heap[i]) { break; }
        M_SwapNode(i, j);
        i = j;
        if(i==0){
            break;
        }
        j = (i - 1) / 2;
    }
}

bool HeapTimer::M_siftdown(size_t index, size_t n)
{
//    assert(index >= 0 && index < m_heap.size());
//    assert(n >= 0 && n <= m_heap.size());
    size_t i = index;
    size_t j = i * 2 + 1;       //左子节点
    while(j < n) {
        if(j + 1 < n && m_heap[j + 1] < m_heap[j]) j++;
        if(m_heap[i] < m_heap[j]) break;
        M_SwapNode(i, j);
        i = j;
        j = i * 2 + 1;
    }
    return i > index;
}

void HeapTimer::M_SwapNode(size_t i, size_t j)
{
//    assert(i >= 0 && i < m_heap.size());
//    assert(j >= 0 && j < m_heap.size());
//    assert( i < m_heap.size());         //无需检查i>=0 因为无符号整数不会小于0
//    assert( j < m_heap.size());
    std::swap(m_heap[i], m_heap[j]);
    m_ref[m_heap[i].id] = i;
    m_ref[m_heap[j].id] = j;
}
