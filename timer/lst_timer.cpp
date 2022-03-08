#include"lst_timer.h"
#include"../log/log.h"
void sort_timer_list::add_timer(util_timer *timer)
{
    //  特殊
    if(!timer) return;
    //  1.  空链表
    if(!head)
    {
        head = tail = timer;
        return ;
    }

    //  2.  如果小于所有定时器，则插在头部，否则加入后面的链表中 
    if(head->expire>timer->expire)
    {
        timer->ne = head;
        head->pre = timer;
        timer->pre = NULL;
        return ;
    }   

    //  3.  否则加入后面的链表中
    add_timer(timer,head);
}

sort_timer_list::~sort_timer_list()
{
    if(!head) return ;
    while(head)
    {
        util_timer *ne = head->ne;
        delete head;
        head = ne;
    }
    return ;
}

void sort_timer_list::adjust_timer(util_timer *timer)
{
    if(!timer || !timer->ne)
        return ;
    if(head==tail)
        return ;
    if(timer->expire<timer->ne->expire)
        return ;
    timer->ne->pre = timer->pre;
    if(head!=timer)
        timer->pre->ne = timer->ne;
    else
    {
        head = timer->ne;
        // timer->ne = NULL;   // 没用我觉得
    }
    add_timer(timer);
}

void sort_timer_list::del_timer(util_timer *timer)
{
    if(!timer) return ;
    //  1个节点
    if(head==tail && head==timer)
    {
        delete head;
        head=tail=NULL;
        return ;
    }

    //  至少2个节点
    util_timer *cur = head;
    while(cur)
    {
        if(cur!=timer) 
        {
            cur = cur->ne;
            continue;
        }
        
        if(timer==head)
        {
            timer->ne->pre = NULL;
            head = timer->ne;
        }
        else
            timer -> pre -> ne = timer -> ne;

        if(timer==tail)
        {
            timer->pre->ne = NULL;
            tail = timer -> pre;
        }
        else
            timer -> ne -> pre = timer -> pre;

        delete timer;
        break;
    }
    if(!cur) return ;
    return ;
}


void sort_timer_list::tick()
{
        if(!head)
        {
            return ;
        }

        LOG_INFO("%s", "time tick");
        Log::get_instance()->flush();
        
        time_t cur = time(NULL);    //  获取系统当时间
        while(head)
        {
            if(head->expire>cur)
                break;
            head->cb_func(head->user_data);
            util_timer *tmp = head;
            head = head->ne;
            del_timer(tmp); //  去除该定时器
        }
}

void sort_timer_list::add_timer(util_timer *timer,util_timer *lst_head)
 {   
    util_timer *cur = lst_head;
    util_timer *pre;
    while(cur)
    {
        if(timer->expire>cur->expire)
        {
            pre = cur;
            cur = cur->ne;
            continue;
        }
        timer->ne = cur;
        timer->pre = cur->pre;
        if(cur->pre)
            cur->pre->ne = timer;
        else 
            head=timer;
        cur->pre = timer;
        break;
    }
    if(!cur)
    {
        pre->ne = timer;
        timer->ne = NULL;
        timer->pre = pre;
        tail = timer;
    }
}