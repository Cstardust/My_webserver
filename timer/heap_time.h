//  问题：时间堆无法在修改节点的时间保证堆有序

/*
如果有一个连接关闭了，我们需要删除他的连接和定时器。但是定时器在堆中，优先队列不支持随即访问，我们也不被允许改变堆中的元素，怎么办呢?。
那么我们就把想要删除的节点打一个标记，为true。那么我们如何打标记呢？
我们不被允许去改变queue中的元素。只能改变queue中元素的副本。
那么我们就把指向util_timer的指针存入queue中。
*/

/*
如何解决任意修改时间节点问题？？
- 手写堆
*/
#ifndef HEAP_H
#define HEAP_H
//  问题：时间堆难以修改节点的时间
#include<algorithm>
#include<queue>
#include<vector>
#include<time.h>
#include<iostream>
#include<arpa/inet.h>
#include<memory>
const int TIMESLOT = 5;     //  超时时间

using std::priority_queue;
using std::vector;

class heap_time;    //  定时器类

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
    util_timer(time_t exp):expire(exp){}    //  为了让时间time可以隐饰转换成util_timer类，方便util_timer对象与时间的比较
    
    util_timer(void (*f) (client_data*),time_t exp,client_data *usd,util_timer *pa,util_timer *pb)
        :cb_func(f),expire(exp),user_data(usd){}
    
    util_timer():cb_func(NULL),expire(0),user_data(NULL){}
    
    bool operator<(const util_timer &rut) const //  比大小
    {
        return this->expire<rut.expire;
    }

    client_data *get_userdata() const
    {
        return user_data;
    }

    bool set_delete(bool flag = true)   //  是否会被删除
    {
        if_delete = flag;
    }

    bool is_delete() const
    {
        return if_delete == true;
    }

    // void update()
    // {
    //     is_valid = true;       //  true代表是修改过的（往大了修改）
    //     time_t cur = time(nullptr);
    //     this->expire = cur + 3*TIMESLOT;
    //     return ;
    // }

public:
    void (*cb_func) (client_data*);  //cb_func是一个函数指针 指向一个函数，这个函数的返回类型是void，形参是client_data*
    time_t expire;  //  发送SIGALRM信号的绝对时间
    client_data *user_data; //  客户资源        用引用不太好 因为引用成员必须列表初始化或者给一个类内初始值
    bool if_delete = false;
    //  if_delete 情况 1. 到时间了  2.对方关闭连接导致我们要关闭连接
    bool is_valid = false;
};


//  仿函数 为了小根堆
struct time_cmp
{
    bool operator()(const std::shared_ptr<util_timer> &pa,const std::shared_ptr<util_timer> &pb)
    {
        return *pa < *pb;
    }
};


class heap_time
{
public:
    using time_node = std::shared_ptr<util_timer>;      // 智能指针
    heap_time() = default;      //  默认构造函数 用类内初始值初始化heap 就是空
    heap_time(const heap_time &) = delete;  //  拷贝构造函数    都没必要
    heap_time & operator= (const heap_time&) = delete;  //  拷贝赋值运算符
    
    void tick();
    bool add_timer(const time_node &);
    bool del_timer(const time_node &);       
    bool adjust_timer(const time_node &);
private:
    // priority_queue<util_timer,vector<util_timer>,time_cmp<util_timer>()> min_heap;  //  小根堆 时间由小到达排序
    priority_queue<time_node,vector<time_node>,time_cmp()> min_heap;
};



//  对于到时间的 我们立刻删除 。对于关闭了连接的。我们最迟经过TIMESLOT时间再关闭
void heap_time::tick()
{
    if(min_heap.empty()) return ;
    time_t cur = time(nullptr); //  获取当前时间
    while(!min_heap.empty())
    {
        time_node node = min_heap.top();     //  shared_ptr
        if(*node < cur ) 
        {
            if(!node->is_valid)
                node->set_delete();     //  如果到时间了 ，又无效 那么就删除
            else
                node->is_valid  =  false;   //  置为无效连接标志。（这是为了恢复修改节点时间之前）
        }
        if(node->is_delete())           //  该删除的话
        {
            node->cb_func(node->get_userdata());        //  关闭连接
            min_heap.pop();
        }
    }
    return ;
}


inline 
bool heap_time::add_timer(const time_node & pt)
{
    min_heap.push(pt);
    return true;
}


//  heap_timer类外如何删除节点？（连接关闭时就调用这个）
inline 
bool heap_time::del_timer(const time_node & pt)
{
    pt->set_delete();
}


inline 
bool heap_time::adjust_timer(const time_node &)
{

}

#endif

//  //  不能用非const引用或者非const指针接收heap的top。heap不允许外界改变他的元素。 因为heap的top返回的是常量引用

/*
// if(*node < cur) break;     //  这里cur会隐士转换成util_timer对象，然后重载的<号就可以被调用了
// node->cb_func(node->get_userdata());  //  到时间 执行应该对这个连接做的事情
// min_heap.pop();     //  弹出到时的定时器。
*/


//  为了造出小根堆
// template<typename T> struct time_cmp{
//     bool operator()(const T &x,const T &y)
//     {
//         return x < y;
//     }
// };
