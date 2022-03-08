#ifndef THREADPOOL_H
#define THREADPOOL_H

#include"locker.h"
#include"./log/log.h"
#include<list>
#include<cstdio>
#include<iostream>

template <typename T>
class threadpool
{
public:
    threadpool(int thread_number = 8,int max_requests = 10000);
    ~threadpool();
    //  向请求队列中添加任务
    bool append(T *request);    //  生产者
private:
    static void* worker(void*);    //  消费者 如果不是static的话 会有隐士this指针
    void run();
private:
    int m_thread_number;    //  线程池中的线程数
    pthread_t *m_threads;   //  描述线程池的数组，大小为m_thraed_number;
    locker m_queuelocker;   //  保护请求队列的互斥锁

    int m_max_requests;     //  请求队列中允许的最大请求数
    sem m_queuestat;        //  是否有任务需要处理
    std::list<T*> m_workqueue;  //  请求队列  

    bool m_stop;            //  是否结束线程
    //  m_workqueue为请求队列（链表） 里面装的是任务 任务max=m_max_request 
    //  是否有任务由sem信号量对象queuestat决定
    //  m_thread_number 为线程池（数组） 里面装的是线程，线程max=m_thread_number
    //  线程同步由互斥锁m_queuelocker决定
};




template< typename T >
threadpool< T >::threadpool(int thread_number ,int max_requests):
    m_thread_number(thread_number),m_threads(NULL),m_max_requests(max_requests),m_stop(false)
{
    if(thread_number<=0 || max_requests<=0 )
        throw std::exception();
    
    m_threads = new pthread_t[m_thread_number]; //  线程池数组 保留线程id号
    if(!m_threads)  
        throw std::exception();
    
    // 创建m_thread_number个线程 并设置线程分离
    for(int i=0;i!=m_thread_number;++i)
    {
        // printf("create the %dth thread\n",i);
        // LOG_INFO("create the %dth thread\n",i);
        // Log::get_instance()->flush();
        if(pthread_create(m_threads+i,NULL,worker,this)!=0) //  create之后线程就开始运行了
        {
            delete [] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i])!=0)
        {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>:: ~threadpool()
{
    m_stop = true;
    delete [] m_threads;
}


template<typename T>
bool threadpool<T>:: append(T *request)
{
    m_queuelocker.lock();
    if(m_workqueue.size()>=m_max_requests)  //  任务数量已经达到上线
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request); //  请求队列是临界资源（所有线程共享） 需要枷锁访问
    m_queuestat.post(); //  信号量++
    m_queuelocker.unlock();
    return true;
}


//  如何在静态成员函数内调用非静态成员变量？
//  传入this指针
template<typename T>
void* threadpool<T>::worker(void *arg)
{
    threadpool *m_this = static_cast<threadpool*>(arg);
    m_this->run();
    return m_this;
}

template<typename T>
void threadpool<T>::run()
{
    while(!m_stop)
    {
        //  看信号量 先看信号量 再上锁 不然先上锁再等信号量会卡死
        m_queuestat.wait(); // --
        //  上锁 要访问共享区域了
        m_queuelocker.lock();
        //  取出请求队列中的任务
        T* t = this->m_workqueue.front();
        m_workqueue.pop_front();
        //  放锁
        m_queuelocker.unlock(); 
        //  执行任务 t.work();
        t->process();   //  由http_conn对象在副线程中执行 解析http请求 生成http响应
        //  t的任务函数执行结束之后 这个http_conn对象之后是否被销毁对于线程池来说都无所谓了。
        //  线程只是负责执行放进来的t对象中的任务。
    }
}


#endif