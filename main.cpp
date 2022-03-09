#include"threadpool.h"
#include"locker.h"
#include"http_conn.h"
#include<sys/socket.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include<sys/signal.h>
#include<cstring>
#include<cassert>
#include<arpa/inet.h>
#include "./timer/lst_timer.h"
#include"./log/log.h"

#define SYNLOG  //同步写日志
#define MAX_FD 65536   // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  // 监听的最大的事件数量
const int TIMESLOT = 5;     //  超时时间
static int epfd;
static int pipefd[2];

extern int setnonblocking(int fd);
//  通常,我们根据recv返回值来判断socket是接收到的有效数据还是对方关闭连接的请求
//  但 EPOLLRHUP事件,在socke上接收到对方关闭连接的请求后触发.
extern void addfd(int epfd,int fd,bool et = true,bool oneshot = true);
static sort_timer_list timer_lst;   //  定时器链表 每个结点都是一个定时器对象
extern void removefd(int epfd,int fd);

//信号处理函数
void sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//  注册捕捉信号sig
void addsig(int sig,void (*func)(int),bool restart = true)
{
    struct sigaction act;
    memset(&act,0,sizeof act);
    act.sa_handler = func;
    sigfillset(&act.sa_mask);
    if(restart) 
        act.sa_flags|=restart;  //  重新调用被中断的系统调用
    assert(sigaction(sig,&act,NULL)!=-1);
}


// 定时处理任务，重新定时以不断触发SIGALRM信号
void timer_handler()
{
    timer_lst.tick();
    alarm(TIMESLOT);
}

/*
目的：对于一些不活跃的连接，我们要即时关闭他们。所以每过一段时间，就要检测这些连接是否到期。
定时器原理：
    为每个连接都造一个定时器。定的”时“就是一个什么时候这些连接到期的时间（绝对时间）到期了，就要把他们关闭
    那么我们怎麼直到这些连接到没到自己相应的时间。？
    将这些定时器作为节点放入一个链表中。（链表是按照到期时间从早到晚排序）
    每过一段时间（5s)，就调用链表的tick函数。即沿着链表按序去检测他们。如果有节点到时间了，就调用相应的处理方法（cb_func)即，关闭连接。
    那么我们如何知道过了5s呢？
    这就需要发送一个SIGALRM信号。alarm(TIMESLOT);
    信号处理函数将捕获到这个信号，并通过管道通知main线程
    在main线程中对这个信号作出处理。即 调用链表的tick函数
        即沿着链表按序去检测他们。如果有节点到时间了，就调用相应的处理方法（cb_func)即，关闭连接。
    然后我们还要再main线程里调用一次alarm（TIMESLOT）。好让之后继续每隔5s检测一次
    循环....
*/

void show_error(int connfd,const char *info)
{
    // printf("%s\n",info);
    // LOG_ERROR("%s\n",info);
    // Log::get_instance()->flush();
    send(connfd,info,sizeof info,0);
    close(connfd);
}


//定时器回调函数，删除非活动连接在socket上的注册事件，并关闭fd ， 并--连接计数
void cb_func(client_data *user_data)
{
    // LOG_INFO("cb_func %d closed",user_data->sockfd);
    // Log::get_instance()->flush();
    epoll_ctl(epfd,EPOLL_CTL_DEL,user_data->sockfd,0);  //  删除注册时间
    assert(user_data);
    close(user_data->sockfd);                           //  关闭文件描述符
    http_conn::m_user_cnt--;                            //  --连接数
}

using namespace std;

int main(int argc,char *argv[])
{
    if(argc<3)
    {
        LOG_INFO("usage : %s ip port ",basename(argv[0]));
        Log::get_instance()->flush();
        return 0;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);
    
    struct sockaddr_in serv_addr;
    memset(&serv_addr,0,sizeof serv_addr);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    threadpool<http_conn> *pool = NULL; //  这时并没有造实例对象，只是一个指针
    try{
        pool = new threadpool<http_conn>(); //  造出线程池对象，里面有多个线程。
    }catch(exception e){
        LOG_ERROR("%s",e.what());
        Log::get_instance()->flush();
        return 1;
    }

    int lfd = socket(AF_INET,SOCK_STREAM,0);
    assert(lfd!=-1);

    // 需要注意的是，设置端口复用函数要在bind绑定之前调用，而且只要绑定到同一个端口的所有套接字都得设置复用：
    // 不是listen之前！！！
    int reuse = 1;
    setsockopt(lfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof reuse);    

    int ret = bind(lfd,(sockaddr*)&serv_addr,sizeof serv_addr);
    assert(ret!=-1);

    ret = listen(lfd,5);    //  这个数量怎么定的?
    assert(ret!=-1);

    epfd = epoll_create(5); //  这个数量怎么定的?
    http_conn::m_epfd = epfd;   //  http_conn类的epfd
    epoll_event events[MAX_EVENT_NUMBER];
    addfd(epfd,lfd,false,false);  //  lfd不能注册EPOLLONESHOT事件,否则只能处理一个连接
    // addfd(epfd,lfd,true,false);  //  监听套接字 ET触发模式下只能接收一个连接诶？？？？？！！

    //创建管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setnonblocking(pipefd[1]);      //  非阻塞写
    addfd(epfd, pipefd[0], false,false);  //  epoll注册读事件 可不能是oneshot啊。  
    //  信号从pipefd[1]中写入，从pipefd[0]中读取。这是信号捕捉函数通知main线程发生了sigalrm信号的方式
    //  当有连接太久不活跃（即发生超时事件），sort_timer_lst对象会检测到，并去处理？/？

    addsig( SIGPIPE, SIG_IGN );
    addsig(SIGALRM, sig_handler, true);
    // alarm(TIMESLOT);    //  5s 后产生一个alarm信号

    http_conn* users = new http_conn[MAX_FD];               //  http连接
    client_data *users_timer = new client_data[MAX_FD];     //  用户资源
    //  不应该叫users_timer 就应该叫用户资源 conn_source
    bool time_to_check = false;
    bool stop_server = false;
    while(!stop_server)
    {
        // epoll先被SIGALRM信号中断，然后while循环又使得其重新调用一次epollwait 使他检测到了那个管道的读事件。
        int ev_num = epoll_wait(epfd,events,MAX_EVENT_NUMBER,-1);
        // std::cout<<"ev_num = "<<ev_num<<std::endl;
        LOG_DEBUG("ev+num = %d",ev_num);
        Log::get_instance()->flush();

        if(ev_num<0 && errno!=EINTR)    //  没有被信号中断，则是出错
        {
            LOG_ERROR("%s","epoll error");
            Log::get_instance()->flush();
            break;
        }
        // cout<<strerror(errno)<<endl;
        LOG_DEBUG("%s",strerror(errno));
        Log::get_instance()->flush();
        for(int i=0;i<ev_num;++i)
        {
            int sockfd = events[i].data.fd;
            // cout<<"sockfd = "<<sockfd<<endl;

            LOG_INFO("scokfd = %d",sockfd);
            Log::get_instance()->flush();
            // LOG_INFO("触发文件描述符 %d",sockfd);
            // Log::get_instance()->flush();
            
            if(sockfd==lfd)     //  建立新连接
            {
                struct sockaddr_in cliet_addr;
                memset(&cliet_addr,0,sizeof 0);
                socklen_t cliet_len = sizeof cliet_addr;
                
                int connfd = accept(lfd,(sockaddr*)&cliet_addr,&cliet_len);
                if(connfd==-1)
                {
                    close(connfd);
                    // printf("errno is %d\n",errno);
                    LOG_ERROR("errno is %d\n",errno);
                    Log::get_instance()->flush();
                    continue;
                }                
                if(http_conn::m_user_cnt>=MAX_EVENT_NUMBER) //  连接数量太多
                {
                    show_error(connfd,"Internal server busy");   //  too many
                    continue;
                }                
                
                //  已经新建立了连接 因为之后要进行http通信 因此将两边的socket放入http_conn对象
                //  这句话，可以接纳新的连接，并且填充在旧的被关闭的连接的http_conn对象上
                //（因为那个之前断开的sockfd被close了，所以这个connfd一定是之前断开导致的空位（如果之前有连接关闭的画）
                users[connfd].init(connfd,cliet_addr); 

                 //初始化该连接对应的连接资源
                users_timer[connfd].cliet_addr = cliet_addr;
                users_timer[connfd].sockfd = connfd;
    
                //创建定时器对象（链表中的一个节点）
                util_timer *timer = new util_timer;
                //设置定时器对应的连接资源
                timer->user_data = &users_timer[connfd];
                //设置回调函数
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                //设置绝对超时时间
                timer->expire = cur + 3 * TIMESLOT;
                //创建该连接对应的定时器，初始化为前述临时变量
                users_timer[connfd].timer = timer;
                
                //将该定时器添加到链表中
                timer_lst.add_timer(timer);
            }
            else if(events[i].events & (EPOLLRDHUP|EPOLLHUP|EPOLLERR))  //  异常
            {
                //  EPOLLRDHUP 似乎是关闭连接？
                // LOG_INFO("%s","disconnect");
                // Log::get_instance()->flush();
                // std::cout<<"disconnected"<<std::endl;
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                timer->cb_func(&users_timer[sockfd]);
                // users[sockfd].close_conn(); //  close + epoll_ctl_del 那么当这个客户端再次连接的时候，岂不是
                if(timer) timer_lst.del_timer(timer);
            }
             //处理信号
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1)      //  
                {
                    continue;
                }
                else if (ret == 0)  
                {
                    continue;
                }
                else
                {
                    for (int i = 0; i < ret; ++i)
                    {
                        switch (signals[i])
                        {
                            case SIGALRM:
                            {
                                // LOG_INFO("%s","SIGALRM信号");
                                // Log::get_instance()->flush();
                                time_to_check = true;
                                break;
                            }
                            case SIGTERM:
                            {
                                stop_server = true;
                                break;
                            }
                        }
                    }
                }
            }
            else if(events[i].events & EPOLLIN) //  读
            {
                util_timer *timer = users_timer[sockfd].timer;  //  该fd的计时器
                if(users[sockfd].read_conn())
                {
                    // LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    // Log::get_instance()->flush();
                    //若监测到读事件，将该事件放入请求队列
                    pool->append(users + sockfd);
                    //若有数据传输，则将定时器往后延迟3个单位
                    //并对新的定时器在链表上的位置进行调整
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        // LOG_INFO("%s", "adjust timer once");
                        // Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);
                    }
                }
                else
                {
                    // std::cout<<"epollin"<<std::endl;
                    //  并不销毁对象，只是把这个对象清空。（之后还得有连接呢）
                    timer->cb_func(timer->user_data);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }
            }
            else if(events[i].events & EPOLLOUT)
            {
                util_timer *timer = users_timer[sockfd].timer;
                if(users[sockfd].write_conn())
                {
                    //若有数据传输，则将定时器往后延迟3个单位
                    //并对新的定时器在链表上的位置进行调整
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        // LOG_INFO("%s", "adjust timer once");
                        // Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);      //  调正节点
                    }
                }
                else
                {
                    timer->cb_func(timer->user_data);
                    timer_lst.del_timer(timer);
                }
            }
            else
            {
                // LOG_WARN("%s","sth else happened");
                // Log::get_instance()->flush();
            }
        }

        if (time_to_check)
        {
            timer_handler();        //  list.tick(); alarm(TIMESLOT);
            time_to_check = false;
        }
    }

    delete [] users;
    delete [] users_timer;
    delete pool;
    close(pipefd[1]);
    close(pipefd[0]);
    close(lfd);
    close(epfd);
    return 0;
}

