#ifndef LOCKER_H
#define LOCKER_H

#include<pthread.h>
#include<exception>
#include<semaphore.h>

//  封装互斥锁
//  pthread_mutex_init
//  pthread_mutex_destroy
//  pthread_mutex_lock
//  pthread_mutex_unlock
class locker
{
public:
    //  初始化锁
    locker();    
    //  销毁互斥锁
    ~locker();
    //  上锁
    bool lock();
    //  解锁
    bool unlock();
    //  获取锁 返回引用
    pthread_mutex_t & get();    
private:
    pthread_mutex_t m_mutex;
};


//  封装条件变量  条件变量一般都和互斥锁配合使用
//  pthread_cond_init()
//  pthread_cond_destory()
//  pthread_cond_wait()
//  pthread_cond_signal()
class cond
{
public:
//  初始化条件变量和互斥锁
    cond();
//  释放互斥锁和条件变量
    ~cond();
//  等待条件变量    有困惑 怎么lock之后直接就wait条件变量了呢。难道不应该先潘达un是否满足条件么。
    bool wait(pthread_mutex_t &);
    bool timewait(pthread_mutex_t &mutex,const struct timespec &t);
//  唤醒一个至少阻塞在条件变量上的线程
    bool signal();
//  唤醒所有阻塞在该条件变量上的线程
    bool broadcast();
private:
    pthread_cond_t m_cond;
};



class sem
{
public:
    sem(int num = 0);   //  初始化信号量大小
    ~sem();
    bool wait();    //  信号量-1
    bool post();    //  信号量+1
private:
    sem_t m_sem;
};

#endif