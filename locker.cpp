#include"locker.h"

locker::locker()
{
    //  创建并初始化互斥锁
    if(pthread_mutex_init(&m_mutex,NULL)!=0)
    {
        throw std::exception();
    }
}

locker::~locker()
{
    pthread_mutex_destroy(&m_mutex);
}

bool locker::lock()
{
    return pthread_mutex_lock(&m_mutex)==0;
}

bool locker::unlock()
{
    return pthread_mutex_unlock(&m_mutex)==0;
}

pthread_mutex_t& locker::get()
{
    return m_mutex;
}


cond::cond()
{
    if(pthread_cond_init(&m_cond,NULL)!=0)
    {
        throw std::exception();
    }
}

cond::~cond()
{
    pthread_cond_destroy(&m_cond);
}

bool cond::wait(pthread_mutex_t &mutex)
{
    return pthread_cond_wait(&m_cond,&mutex)==0;
}


bool cond::timewait(pthread_mutex_t &mutex,const struct timespec &t)
{
    return pthread_cond_timedwait(&m_cond,&mutex,&t)==0;
}

bool cond::signal()
{
    return pthread_cond_signal(&m_cond)==0;
}

bool cond::broadcast()
{
    return pthread_cond_broadcast(&m_cond)==0;
}
// int pthread_cond_wait(pthread_cond_t *__restrict__ __cond,
// pthread_mutex_t *__restrict__ __mutex)
// 阻塞等待条件变量cond（参1）满足
// 释放已掌握的互斥锁（解锁互斥量）相当于pthread_mutex_unlock(&mutex);
//      1.2.两步为一个原子操作。
// 当被唤醒，pthread_cond_wait函数返回时，解除阻塞并重新申请获取互斥锁pthread_mutex_lock(&mutex)


sem::sem(int num)
{
    if(sem_init(&m_sem,0,num)!=0)
    {
        throw std::exception();
    }
}

sem::~sem()
{
    sem_destroy(&m_sem);
}

bool sem::wait()
{
    return sem_wait(&m_sem)==0;
}

bool sem::post()
{
    return sem_post(&m_sem)==0;
}




