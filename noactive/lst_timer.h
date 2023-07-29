/*
    定时去检测非活跃的连接
    升序的双链表保存定时器
*/

#ifndef LST_TIMER_H
#define LST_TIMER_H

#include <stdio.h>
#include <time.h>
#include <arpa/inet.h>

#include "http_conn.h"

//#define BUF_SIZE 64

//class util_timer;    //前向声明

class http_conn;    //前向声明

//用户数据结构（将用户数据结构写在http里,不声明在这
//struct client_data{
//    sockaddr_in address;    //客户端socket地址
//    int sockfd;             //socket文件描述符
//    char buf[BUF_SIZE];     //读缓存（移植到项目中就不需要读缓存
//    util_timer* timer;       //定时器
//};

//定时器类
class util_timer
{
public:
    util_timer():prev(NULL),next(NULL){}

public:
    time_t exprie;      //任务超时时间，这里使用绝对时间
//    void (*cb_func)(http_conn*);  // 任务回调函数，回调函数处理的客户数据，由定时器的执行者传递给回调函数
    http_conn* user_data;
    util_timer* prev;    // 指向前一个定时器
    util_timer* next;    // 指向后一个定时器
};

// 定时器链表，它是一个升序、双向链表，且带有头节点和尾节点。
class sort_timer_lst {
public:
    sort_timer_lst() : head( NULL ), tail( NULL ) {}
    // 链表被销毁时，删除其中所有的定时器
    ~sort_timer_lst();

    // 将目标定时器timer添加到链表中
    void add_timer( util_timer* timer );
    /* 当某个定时任务发生变化时，调整对应的定时器在链表中的位置。这个函数只考虑被调整的定时器的
    超时时间延长的情况，即该定时器需要往链表的尾部移动。*/
    void adjust_timer(util_timer* timer);
    // 将目标定时器 timer 从链表中删除
    void del_timer( util_timer* timer );
    /* SIGALARM 信号每次被触发就在其信号处理函数中执行一次 tick() 函数，以处理链表上到期任务。*/
    void tick();

private:
    /* 一个重载的辅助函数，它被公有的 add_timer 函数和 adjust_timer 函数调用
    该函数表示将目标定时器 timer 添加到节点 lst_head 之后的部分链表中 */
    void add_timer(util_timer* timer, util_timer* lst_head);

private:
    util_timer* head;   // 头结点
    util_timer* tail;   // 尾结点
};

#endif // LST_TIMER_H
