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

#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
#include "noactive/lst_timer.h"
#include "log.h"

#define MAX_FD 65535   //最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  //一次监听的最大数量

static int pipefd[2];           // 管道文件描述符 0为读，1为写

//添加文件描述符到epoll中
extern void addfd(int epollfd,int fd,bool one_shot,bool et);
//删除文件描述符
extern void removefd(int epollfd,int fd);
//修改文件描述符
extern void modfd(int epollfd,int fd,int ev);
// 文件描述符设置非阻塞操作
extern void setnonblocking(int fd);


//添加信号捕捉
void addsig(int sig,void(handler)(int))
{
    struct sigaction sa;            // sig 指定信号， void handler(int) 为处理函数
    memset(&sa,'\0',sizeof(sa));    // bezero 清空
    sa.sa_flags = 0;                        // 调用sa_handler
    // sigact.sa_flags |= SA_RESTART;                  // 指定收到某个信号时是否可以自动恢复函数执行，不需要中断后自己判断EINTR错误信号
    sa.sa_handler=handler;          // 指定回调函数
    //设置临时阻塞信号集
    //将所有信号添加到sa.sa_mask信号集中，这样在处理信号时，将所有信号都临时阻塞，防止信号处理函数被其他信号中断
    sigfillset(&sa.sa_mask);        // 将临时阻塞信号集中的所有的标志位置为1，即都阻塞
    sigaction(sig,&sa,NULL);        // 设置信号捕捉sig信号值
}

// 向管道写数据的信号捕捉回调函数
void sig_to_pipe(int sig){
    int save_errno = errno;
    int msg = sig;
    send( pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}

int main(int argc,char* argv[])
{
    if(argc<=1){    // 形参个数，第一个为执行命令的名称
//        printf("按照如下格式运行：%s port_number\n",basename(argv[0]));
        EMlog(LOGLEVEL_ERROR,"run as: %s port_number\n", basename(argv[0]));      // argv[0] 可能是带路径的，用basename转换
        exit(-1);
    }

    //获取端口号
    int port=atoi(argv[1]);

    //对SIGPIE信号进行处理
    addsig(SIGPIPE,SIG_IGN);

    int listenfd=socket(PF_INET,SOCK_STREAM,0);
    assert( listenfd >= 0 );                            // ...判断是否创建成功

    //设置端口复用
    int reuse=1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    //绑定
    struct sockaddr_in address;
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=INADDR_ANY;
    address.sin_port=htons(port);
    int ret = bind(listenfd,(struct sockaddr*)&address,sizeof(address));
    assert( ret != -1 );    // ...判断是否成功

    //监听
    ret=listen(listenfd,8);
    assert( ret != -1 );    // ...判断是否成功

    //创建epoll对象，事件数组，添加（IO多路复用，同时检测多个事件）
    epoll_event events[MAX_EVENT_NUMBER];   // 结构体数组，接收检测后的数据
    int epollfd=epoll_create(5);    // 参数 5 无意义， > 0 即可
    assert( epollfd != -1 );

    //将监听的文件描述符添加到epoll对象中
    addfd(epollfd,listenfd,false,false); // 监听文件描述符不需要 ONESHOT & ET

    // 创建管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert( ret != -1 );
    setnonblocking( pipefd[1] );               // 写管道非阻塞
    addfd(epollfd, pipefd[0], false, false ); // epoll检测读管道

    // 设置信号处理函数
    addsig(SIGALRM, sig_to_pipe);   // 定时器信号
    addsig(SIGTERM, sig_to_pipe);   // SIGTERM 关闭服务器
    bool stop_server = false;       // 关闭服务器标志位



    //创建一个数组用于保存所有的客户端信息(在http_conn类里，更好的办法是分开来
    http_conn * users=new http_conn[MAX_FD];
    http_conn::m_epollfd=epollfd;   // 静态成员，类共享

    //创建线程池，初始化线程池
    //任务：http连接的任务
    threadpool<http_conn> * pool=NULL;
    try{
        pool=new threadpool<http_conn>();
    }catch(...){
        exit(-1);
    }

    bool timeout = false;   // 定时器周期已到
    alarm(TIMESLOT);        // 定时产生SIGALRM信号

    while(!stop_server){
        // 检测事件
        int num=epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1); // 阻塞，返回事件数量
        if(num<0 && errno!= EINTR){
//            printf("epoll failure!\n");
            EMlog(LOGLEVEL_ERROR,"EPOLL failed.\n");
            break;
        }

        //循环遍历事件数组
        for(int i=0;i<num;i++){
            int sockfd=events[i].data.fd;
            if(sockfd==listenfd){   // 监听文件描述符的事件响应
                //有客户端连接进来

                struct sockaddr_in client_address;
                socklen_t client_addrlen=sizeof(client_address);
                int connfd=accept(listenfd,(struct sockaddr*)&client_address,&client_addrlen);

                if( http_conn::m_user_count >= MAX_FD ){
                    //目前连接数满了

                    //给客户端写一个信息:服务器内部正忙

                    close(connfd);
                    continue;
                }

                //将新的客户的数据初始化，放到数组中
                users[connfd].init(connfd,client_address);
                // conn_fd 作为索引
                // 当listen_fd也注册了ONESHOT事件时(addfd)，
                // 接受了新的连接后需要重置socket上EPOLLONESHOT事件，确保下次可读时，EPOLLIN 事件被触发
                // modfd(epoll_fd, listen_fd, EPOLLIN);
            }else if(sockfd == pipefd[0] && (events[i].events & EPOLLIN)){
                // 读管道有数据，SIGALRM 或 SIGTERM信号触发
//                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if(ret == -1){
                    continue;
                }else if(ret == 0){
                    continue;
                }else{
                    for(int i = 0; i < ret; ++i){
                        switch (signals[i]) // 字符ASCII码
                        {
                        case SIGALRM:
                            // 用timeout变量标记有定时任务需要处理，但不立即处理定时任务
                            // 这是因为定时任务的优先级不是很高，我们优先处理其他更重要的任务。
                            timeout = true;
                            break;
                        case SIGTERM:
                            stop_server = true;
                        }
                    }
                }
            }else if(events[i].events& (EPOLLRDHUP|EPOLLHUP|EPOLLERR)){
                //对方异常断开或者错误等事件
                EMlog(LOGLEVEL_DEBUG,"-------EPOLLRDHUP | EPOLLHUP | EPOLLERR--------\n");
                users[sockfd].close_conn();
                // 移除其对应的定时器
                http_conn::m_timer_lst.del_timer(users[sockfd].timer);

            }else if(events[i].events & EPOLLIN ){
                //有读的事件发生
                EMlog(LOGLEVEL_DEBUG,"-------EPOLLIN-------\n\n");
                if(users[sockfd].read()){
                    //一次把所有数据读出来
                    pool->append(users+sockfd);
                }else{
                    //读失败或者没读到数据
                    users[sockfd].close_conn();
                    http_conn::m_timer_lst.del_timer(users[sockfd].timer);  // 移除其对应的定时器
                }
            }else if(events[i].events &EPOLLOUT){
                //写事件发生
                EMlog(LOGLEVEL_DEBUG, "-------EPOLLOUT--------\n\n");
                if(!users[sockfd].write()){
                    //一次性写完数据,写失败了
                    users[sockfd].close_conn();
                    http_conn::m_timer_lst.del_timer(users[sockfd].timer);  // 移除其对应的定时器
                }
            }
        }
        // 最后处理定时事件，因为I/O事件有更高的优先级。当然，这样做将导致定时任务不能精准的按照预定的时间执行。
        if(timeout) {
            // 定时处理任务，实际上就是调用tick()函数
            http_conn::m_timer_lst.tick();
            // 因为一次 alarm 调用只会引起一次SIGALARM 信号，所以我们要重新定时，以不断触发 SIGALARM信号。
            alarm(TIMESLOT);
            timeout = false;    // 重置timeout
        }
    }

    close(epollfd);
    close(listenfd);

    close(pipefd[1]);
    close(pipefd[0]);

    delete[] users;
    delete pool;

    return 0;
}

