//  升序定时器连表
//  定时器设计
//  	连接资源：客户端套接字地址、文件描述符、定时器
//  	定时事件：这就是信号提醒我们要做的事情。回调函数，用户自定义，用于删除非活跃套接字
//  	超时时间：浏览器和服务器连接时刻 + 固定事件TIMEOUT
//  定时器容器设计
//  	升序链表

#ifndef LST_TIMER_H
#define LST_TIMER_H

#include<time.h>
#include<iostream>
#include<arpa/inet.h>

class util_timer;    //  定时器类

//  连接资源
struct client_data
{
    sockaddr_in cliet_addr; // 客户端套接字地址
    int sockfd; //  文件描述符
    util_timer *timer;  //  定时器。必须用指针 因为class没定义呢。引用似乎也可。
};

class util_timer
{
public:
    util_timer(void (*f) (client_data*),time_t exp,client_data *usd,util_timer *pa,util_timer *pb)
        :cb_func(f),expire(exp),user_data(usd),ne(pa),pre(pb){}
    util_timer():cb_func(NULL),expire(0),user_data(NULL),ne(NULL),pre(NULL){}
public:
    void (*cb_func) (client_data*);  //cb_func是一个函数指针 指向一个函数，这个函数的返回类型是void，形参是client_data*
    time_t expire;  //  发送SIGALRM信号的绝对时间
    client_data *user_data; //  客户资源
    util_timer *ne;
    util_timer *pre;    
};


//  定时器链表 升许 双向 具有头尾节点
class sort_timer_list
{
public:
    sort_timer_list():head(NULL),tail(NULL){}
    //  销毁链表时销毁所有定时器
    ~sort_timer_list();
    
    //  向链表中添加定时器
    void add_timer(util_timer *timer);

    //  调整定时器对应的位置 (只考虑时间延长的情况)
    void adjust_timer(util_timer *timer);

    //  将目标定时器移除
    void del_timer(util_timer *timer);

    //  SIGALRM每次被触发就在其信号处理函数（或者主函数（统一事件源时））调用一次tick，以处理定时器上到期的任务。
    void tick(); //  起搏函数
    
private:
    //  将定时器timer添加到lst_head之后的部分链表中(假设timer增大引起的，且lst_head!=NULL)
    void add_timer(util_timer *timer,util_timer *lst_head);
    util_timer *head;
    util_timer *tail;

};

#endif