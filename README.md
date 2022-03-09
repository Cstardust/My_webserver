# My_webserver 1.0版本 2022.2（一定会有2.0的！）

## 学习自 游双《Linux高性能服务器编程》

## 功能
- 智齿http协议、接收get请求，返回相应图片、视频。（主线程负责监听连接，业务线程负责解析请求报文，生成响应报文）
- 注册功能：待实现。。。。

## 主要工作
- 主线程用EPOLL技术实现IO多路复用。监测读socket、写socket事件
- 处理模式：同步IO模拟Proactor模式
- 使用有限状态机解析HTTP请求报文，可以解析GET 请求 //  和POST请求。
- 多线程，增加并发服务数量。

## 难点
- 实现线程池，提高吞吐量
  - 信号量实现对任务的互斥，互斥锁实现线程同步。
- 主从状态机对HTTP报文进行解析
- 链表实现定时器，处理不活跃连接。（服务器程序处理IO事件、信号和定时事件，统一事件源，是指将信号事件与其他事件一样被处理。）


## 收获
- 入门Linux网络编程、Linux系统编程。
- 加深了对TCP三次握手四次挥手的理解
- 了解了http协议
- 对Web服务器服务过程有基本了解
![image](https://user-images.githubusercontent.com/73826377/157267362-16c44478-daa4-433c-b819-88d415db2dcf.png)
![image](https://user-images.githubusercontent.com/73826377/157267912-e83e9c7d-72a4-4e20-9771-f9ce513c4a1e.png)

![7ee72e43daab159b427299359555420](https://user-images.githubusercontent.com/73826377/157266380-aa52e25b-a834-4295-bb06-a2a2ce47137d.png)


## 未解决问题
- 实现时间堆（使用`priority_queue`作为堆）时，可以立刻删除堆顶，也可延迟删除任意节点，但无法修改节点时维持堆顺序。
  - 解决方法：或许应该手写堆，这样就可以做到任意的修改节点。
- 对监听套接字采用ET模式时并发量远小于LT模式时。
- 会莫名更改http_conn对象的m_sockfd
  - 猜测：
     - 前一个http_conn对象的读/写缓冲区溢出，覆盖了相邻对象。
```c++
int epfd = epoll_create();
addfd(listening_fd);    //  设置fd为非阻塞，并注册EPOLLIN | EPOLLET事件
alarm(TIMESLOT);        //  唤醒定时器
while(1)
{
  int ev_num = epoll_wait(epfd,events,MAX_EVENT_NUMEBR,-1);
  LOG_DEBUG("errno = %s",strerror(errno));
  //ET和LT下都会发生 errno = Resource temporarily unavailable 或者 alarm信号打断的Interrupted system call
  for(int i=0;i<ev_num;++i) //  处理发生的事件
  {
    if(events[i].fd==lfd) //  接收连接
    {
      accept(lfd,(sockaddr*)&client_addr,&client_len);
      ...
    }
    else //  读socket事件
    else //  写socket事件
    else //  信号
  }
}

listening_fd 为LT 模式下
$ ./webbench -c 1000 -t 60 http://192.168.147.180:12345/index.html
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://192.168.147.180:12345/index.html
1000 clients, running 60 sec.

Speed=527795 pages/min, 1407368 bytes/sec.
Requests: 527790 susceed, 5 failed.
===============================================================
listening_fd 为ET模式下
$ ./webbench -c 1000 -t 60 http://192.168.147.180:12345/index.html
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://192.168.147.180:12345/index.html
1000 clients, running 60 sec.

Speed=11 pages/min, 24 bytes/sec.
Requests: 11 susceed, 0 failed.
```


## 下一步目标
- vector<char>实现缓冲区动态增长
- 日志IO
- 正则表达式解析报文
- 加入数据库功能
