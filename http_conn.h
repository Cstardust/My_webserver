#include<arpa/inet.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<unistd.h>
#include<iostream>
#include<sys/stat.h>
#include<sys/mman.h>
#include <stddef.h>
#include<stdarg.h>
#include <sys/uio.h>
#include<cstring>
#include<unordered_map>
#include "log/log.h"

void modfd(int epfd,int fd,int ev)
{
    struct epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
    epoll_ctl(epfd,EPOLL_CTL_MOD,fd,&event);
}

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

// 网站的根目录
const char *doc_root = "/home/shc/code/my_webserver/resource";

//  每个http_conn对象负责解析本对象所占据的socket，收到的数据 并生成answer
class http_conn
{
public:
    http_conn(){}
    ~http_conn(){}
    void process(); //  由线程池中的工作线程调用 是处理http请求的入口函数
    void init(int ,const sockaddr_in&);     //  初始化本对象所负责的http连接的信息（如socket和对端地址）
    void close_conn();
    bool read_conn();   //  非阻塞读
    bool write_conn();  //  非阻塞写 都是占用main线程

public:
    static int m_epfd;    //  共用的epoll。所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
    static int m_user_cnt;  //  用户数量
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    static const int FILENAME_LEN = 200;
    //  http请求方法，这里只支持GET
    enum method {GET,POST,HEAD,PUT,DELETE,TRACE,OPTION,CONNECT};

    //  主状态机    
    //  CHECK_REQUEST_LINE 解析请求行             
    //  CHECK_STATE_HEADER 解析请求头             
    //  CHECK_STATE_CONTENT 解析请求体（POST）
    enum CHECK_STATE{CHECK_STATE_REQUESTLINE , CHECK_STATE_HEADER , CHECK_STATE_CONTENT};
    
    //  从状态机 
    //  LINE_OK 完整一行
    //  LINE_BAD 语法错误
    //  LINE_OPEN 读取的行不完整
    enum LINE_STATE{LINE_OK,  LINE_BAD,  LINE_OPEN};

    /*
        //  HTTP请求处理结果
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE{NO_REQUEST,GET_REQUEST,BAD_REQUEST,NO_RESOURCE,FORBIDDEN_REQUEST,FILE_REQUEST,INTERNAL_ERROR,CLOSED_CONNECTION};

private:
    //  解析报文整体流程
    HTTP_CODE process_read();   
    
    //  从状态机：从m_read_buf中不断尽可能解析出完整的无措的一行。
    //  驱动主状态机运行
    LINE_STATE parse_line();    

    //  主状态机的状态转换函数    
    //  解析请求行
    HTTP_CODE parse_requestline(char *text_line);
    //  解析请求头
    HTTP_CODE parse_header(char *text_line);
    //  解析请求体（POST）
    HTTP_CODE parse_content(char *text_line);

    //  获取报文中的一行数据 （一行的开头）
    char *get_line();   

    //  处理报文提出的需求：将报文请求的file经mmap映射到内存中去
    HTTP_CODE do_request(); 

    //  生成报文响应：将内容放入iovec数组中 为了之后writev分散写到m_sockfd中
    //  iovec 1： 放入响应报文
    //  iovec 2:  放入内容（字符串 文件等）
    bool process_write(HTTP_CODE ret);
    // 这一组函数被process_write调用以填充HTTP应答。
    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type();
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();

    void init();

private:
    //  主状态机的状态
    CHECK_STATE m_check_state;
    //  该http连接的socket和对方的socket地址
    //  负责的socket
    int m_sockfd;         
    //  对端地址
    sockaddr_in m_cliet_addr;   
    //  存储着读取的请求报文
    char m_read_buf[READ_BUFFER_SIZE];
    //  缓冲区中m_read_buf中数据的最后一个字节的下一个位置(即下一次读如的数据的存放起点：m_read_buf  + m_read_idx)  
    int m_read_idx;     
    //  当前正在解析的位置（相当于for遍历时的i）    
    int m_checked_idx;  
    //  从m_read_buf中已经解析的字符个数，m_read_buf + m_start_line 就是下一次该开始解析的地方。
    //  也就是每一个数据行在m_read_buf中的起始位置（一次解析一行麻）
    int m_start_line;   
    int m_write_idx;
    char m_write_buf[WRITE_BUFFER_SIZE];    //  存储发出的响应报文数据
    char m_real_file[ FILENAME_LEN ];       // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录
    //  客户请求的目标文件名
    char *m_url;
    //  协议版本
    char *m_version;
    //  请求方法
    method m_method;     
    //  请求资源所在的主机
    char *m_host;
    //  请求体长度
    int m_content_len;
    //  是否保持连接
    bool m_linger;

    struct stat m_file_stat;    //  文件信息结构体
    char *m_file_address;   //  文件映射到的内存地址
    struct iovec m_iv[2];   //  iovec数组
    int m_iv_count;         //  数组大小
    int bytes_to_send;      //  需要发送的大小
    int bytes_have_send;    //  已经发送的大小
    int cgi;    //  是否启动post

};


// 写HTTP响应
//  return false 写完之后关闭该连接
//  return true 写完之后保持该链接
bool http_conn::write_conn()
{
    int temp = 0;
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        modfd( m_epfd, m_sockfd, EPOLLIN ); 
        init(); //  m_read_idx清0
        return true;
    }

    while(1) {
        // 分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if ( temp <= -1 ) {
            //  写缓冲满的时候，不会写入到缓冲区中。能写入到缓冲区中，返回的temp自然>=0。
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            //  等待下一次写 保持数据，等待下一次写的机会。
            if( errno == EAGAIN ) {
                modfd( m_epfd, m_sockfd, EPOLLOUT );
                return true;
            }

            //  不是缓冲区问题，则放弃文件内容 return false 主线程中关闭该连接。
            unmap();        //  
            return false;
        }

        bytes_have_send += temp;        //  已经写入socket写缓冲区的
        bytes_to_send -= temp;          //  需要写入socket写缓冲区的

        if (bytes_have_send >= m_iv[0].iov_len) //  响应报文除了消息体（文件）已经写完
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);        //  文件起点
            m_iv[1].iov_len = bytes_to_send;                                            //  剩下的长度
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;           //  响应报文没写完 接着写
            m_iv[0].iov_len = m_iv[0].iov_len - temp;                   //  剩下的长度
        }

        if (bytes_to_send <= 0)
        {
            // 没有数据要发送了
            unmap();
            modfd(m_epfd, m_sockfd, EPOLLIN);       //  写完了 该等待这个fd有数据需要读了

            if (m_linger)
            {
                init();         //  保持连接
                return true;
            }
            else
            {
                return false;   //  写完即关闭（main中 write完之后close fd)
            }
        }
    }
}

//  已经写入：[m_write_buf,m_write_buf + m_write_idx)
//  剩余空间：[m_write_buf + m_write_idx,m_write_buf + BUFFER_SIZE)
bool http_conn::add_response(const char *format,...)
{
     //如果写入内容超出m_write_buf大小则报错
    if( m_write_idx >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    //定义可变参数列表
    va_list arg_list;
    //将变量arg_list初始化为传入参数
    va_start( arg_list, format );
    //将数据format从可变参数列表写入缓冲区写，返回写入数据的长度 -1是为了字符串的'\0'？？
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
     //如果写入的数据长度超过缓冲区剩余空间，则报错
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {
        return false;
    }
    //更新m_write_idx位置
    m_write_idx += len;         //  移动m_write_idx
    //清空可变参列表
    va_end( arg_list );
    
    return true;
}


//  1   HTTP/1.1 200 OK
//  2   Date: Fri, 22 May 2009 06:07:21 GMT
//  3   Content-Type: text/html; charset=UTF-8
//  4   空行
//  5   <html>
//  6          <head></head>
//  7       <body>
//  8                <!--body goes here-->
//  9       </body>
// 10   </html>
// 添加消息报头，具体的添加文本长度、连接状态和空行
bool http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}

//添加状态行
bool http_conn::add_status_line(int status,const char *title)
{
    return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
}
//添加空行
bool http_conn::add_blank_line()
{
    add_response("%s","\r\n");
}
//添加连接状态，通知浏览器端是保持连接还是关闭
bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n",m_linger==true ? "keep-alive" : "close");
}
//添加Content-Length，表示响应报文的长度
bool http_conn::add_content_length(int content_length)
{
    return add_response("Content-Length: %d\r\n",content_length);
}
//添加文本类型，这里是html
bool http_conn::add_content_type()
{
    return add_response("Content-Type: %s\r\n","text/html");
}
//添加文本content
bool http_conn::add_content(const char *content)
{
    return add_response("%s",content);
}

void http_conn::unmap()
{
    if(m_file_address)
        munmap(m_file_address,m_file_stat.st_size); //  释放内存
    m_file_address = nullptr;
}

/**
 * HTTP/1.1 400
 * Content-Length: %d\r\n
//  * Content-Type: text/html\r\n
 * Connection: close\r\n
 * \r\n
 * Your request has bad syntax or is inherently impossible to satisfy.\n
 * **/
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
        case INTERNAL_ERROR:
            //  状态行
            add_status_line( 500, error_500_title );
            //  消息报头
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {    //  消息体
                return false;
            }
            break;
        case BAD_REQUEST:       //  语法错误
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title );
            add_headers(m_file_stat.st_size);
            //第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_idx;
            //第二个iovec指针指向mmap返回的文件指针，长度指向文件大小
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            //发送的全部数据为响应报文头部信息和文件大小
            bytes_to_send = m_write_idx + m_file_stat.st_size; 
            //  m_write_idx：响应报文大小 m_file_stat.st_size：准备的文件大小

            return true;
        default:
            return false;
    }
    //   只有响应报文 ， 没有准备文件的情况
    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;//  （只需发送没有内容的报文 那么大小自然就是m_write_idx)
    return true;
}


//  为什么要单独列出来一个这个init?
void http_conn::init()
{
    // std::cout<<m_epfd<<" "<<"init"<<std::endl;
    bytes_to_send = 0;
    bytes_have_send = 0;

    m_check_state = CHECK_STATE_REQUESTLINE;    // 初始状态为检查请求行
    m_linger = false;       // 默认不保持链接  Connection : keep-alive保持连接
// why false
    m_method = GET;         // 默认请求方式为GET
    m_url = 0;              
    m_version = 0;
    m_content_len = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;

    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, READ_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}


//  从m_read_buf中解析出完整的一行(通过移动m_checked_idx来判断解析到哪里(开区间))
//  \r
    //  是否有下一个，以及下一个是否是\n
//  \n
    //  是否有上一个，以及上一个是否是\r
http_conn::LINE_STATE http_conn::parse_line()
{
    for(;m_checked_idx!=m_read_idx;++m_checked_idx)
    {
        char& t = m_read_buf[m_checked_idx];
        if(t=='\r')
        {
            if(m_checked_idx+1<m_read_idx)
            {
                char & t2 = m_read_buf[m_checked_idx+1];
                if(t2=='\n')
                {
                    m_read_buf[m_checked_idx++]='\0';
                    m_read_buf[m_checked_idx++]='\0';
                    return LINE_OK;     //  \r \n -> \0 \0
                }
                else return LINE_BAD;   //  \r X 
            }
            else return LINE_OPEN;      //  \r 到头了   之后会return NoREQUEST 到process里面 然后重置事件 继续监听sockfd 然后等待socket继续读取到m_read_buf中。
        }
        else if(t=='\n')
        {
            //  \r \n -> \0 \0
            if(m_checked_idx>1&&m_read_buf[m_checked_idx-1]=='\r')
            {
                m_read_buf[m_checked_idx-1]='\0';
                m_read_buf[m_checked_idx++]='\0';
                return LINE_OK;
            }
            else return LINE_BAD;
        }
    }

    return LINE_OPEN;   // 没找到\r \n
    //  LINE_OPEN和LINE_OK最后的处理方式一样？？都是modfd?
}


/*
m_url为请求报文中解析出的请求资源，以/开头，也就是/xxx，项目中解析后的m_url有8种情况。
GET请求，跳转到judge.html，即欢迎访问页面
/index.html 或者 /judge.html 或者/vedio.html
*/
// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request()
{
    // "/home/nowcoder/webserver/resources"
    //  先将m_real_file预备成网站根目录
    strcpy( m_real_file, doc_root );
    int len = strlen( doc_root );
    //  寻找m_url中/的位置
    const char *p = strchr(m_url,'/');
    
    // 这里的情况是welcome界面，请求服务器上的一个图片（index.html)
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功. 
    //  NO_RESOURCE 资源不存在
    if ( stat( m_real_file, &m_file_stat ) < 0 ) {
        return NO_RESOURCE;
    }

    // 判断访问权限 是否可读，不可读则返回FORBIDDEN_REQUEST状态
    if ( ! ( m_file_stat.st_mode & S_IROTH ) ) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录 如果是目录，则返回BAD_REQUEST，表示请求报文有误
    if ( S_ISDIR( m_file_stat.st_mode ) ) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open( m_real_file, O_RDONLY );
    // 创建内存映射 通过mmap将该文件映射到内存中
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd ); //关闭文件描述符避免浪费
    return FILE_REQUEST;

}



// 解析HTTP请求行，获得请求方法，目标URL,以及HTTP版本号
//  支持 ：http https 协议
//  支持方法：POST GET（POST具体还没有实现）
//  请求资源：judge.html，index.html    (judge.html 还没添加)
http_conn::HTTP_CODE http_conn::parse_requestline(char *textline)
{
    //     m_url       m_version
    // GET /index.html HTTP/1.1s
    m_url = strpbrk(textline, " \t"); // 判断第二个参数中的字符哪个在text中最先出现
    if (! m_url) { 
        return BAD_REQUEST;
    }
    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';    // 置位空字符，字符串结束符
    char* method = textline;
    if ( strcasecmp(method, "GET") == 0 ) { // 忽略大小写比较
        m_method = GET;
    } else if(strcasecmp(method,"POST")==0){
        m_method = POST;
        cgi=1;
    }else {
        return BAD_REQUEST;
    }

    // /index.html HTTP/1.1
    // 检索字符串 str1 中第一个在字符串 str2 中出现的字符下标。
    m_version = strpbrk( m_url, " \t" );
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    if (strcasecmp( m_version, "HTTP/1.1") != 0 ) {
        return BAD_REQUEST;
    }
    /**
     * 请求资源的两种形式：
     * 1.  http://192.168.110.129:10000/index.html
     * 2.  /index.html
     * 新增：http://192.168.110.129:10000/
     * 对应judge.html
    */
    if (strncasecmp(m_url, "http://", 7) == 0 ) {   
        m_url += 7;
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        m_url = strchr( m_url, '/' );
    }

    if(strncasecmp(m_url,"https://",8)==0)
    {
        m_url += 8;
        m_url = strchr(m_url,'/');  //  m_url = "/index.html";
    }

    if ( !m_url || m_url[0] != '/' ) {
        return BAD_REQUEST;
    }
    if(strlen(m_url)==1)
    {
        strcat(m_url,"judge.html");     //  m_url += "judge.html";
    }

    m_check_state = CHECK_STATE_HEADER; // 检查状态变成检查头
    return NO_REQUEST;
}


inline char* http_conn::get_line()
{
    return m_start_line + m_read_buf;   //  一行起始
}


http_conn::HTTP_CODE http_conn::parse_header(char *textline)
{
    // 遇到空行，表示头部字段解析完毕
    if( textline[0] == '\0' ) {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if ( m_content_len != 0 ) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if ( strncasecmp( textline, "Connection:", 11 ) == 0 ) {
        // 处理Connection 头部字段  Connection: keep-alive
        textline += 11;     //  textline=\tkeep-live
        textline += strspn( textline, " \t" );      //  该函数返回 str1 中第一个不在字符串 str2 中出现的字符下标，也即初始段匹配长度 
        //  使得textline作为:后面有效内容的起点 即textline=keep-live(也即textline[0]=k)
        //  textline=keep-line
        if ( strcasecmp( textline, "keep-alive" ) == 0 ) {
            m_linger = true;
        }
    } else if ( strncasecmp( textline, "Content-Length:", 15 ) == 0 ) {
        // 处理Content-Length头部字段    POST请求才有
        textline += 15;
        textline += strspn( textline, " \t" );
        m_content_len = atol(textline);
    } else if ( strncasecmp( textline, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        textline += 5;
        textline += strspn( textline, " \t" );
        m_host = textline;
    } else {
        LOG_INFO( "oop! unknow header %s\n", textline );
        Log::get_instance()->flush();
    }
    return NO_REQUEST;
}


//  对于POST请求
// 我们没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入了  
http_conn::HTTP_CODE http_conn::parse_content( char* text ) {
    if ( m_read_idx >= ( m_content_len + m_checked_idx ) )  //  我觉得应该是
    {
        text[ m_content_len ] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

/*
*   GET /images/image2.jpg HTTP/1.1
    Host: 192.168.147.175:12345
    Connection: keep-alive
    User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/98.0.4758.102 Safari/537.36
    Accept: image/avif,image/webp,image/apng,image/svg+xml,image/*,*//*;q=0.8
    Referer: http://192.168.147.175:12345/
    Accept-Encoding: gzip, deflate
    Accept-Language: en-GB,en-US;q=0.9,en;q=0.8
*
*/
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATE line_state = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text_line;
    //  第一个条件 未知 ； 第二个条件：完整读取了一行，那么自然就可以继续向下走了
    //  （主状态机调用函数解析读到的该行数据，并依此尝试改变状态）
    while((m_check_state==CHECK_STATE_CONTENT && line_state == LINE_OK)||
                            (line_state=parse_line())==LINE_OK)
    {
        text_line = get_line();  //  获取一行的开头
        m_start_line = m_checked_idx; //  更新m_start_line到下一行的开头
        // printf( "got 1 http line: %s\n", text_line);
        LOG_INFO("got 1 http line: %s\n",text_line);
        Log::get_instance()->flush();
        switch(m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                //  解析请求行的结果。
                //  NO_REQUEST 请求不完整，需要继续读取客户数据。（指的是从存入read_buf的报文中读取，即已经被parse_line分析处理过的行
                //  不是socket缓冲区）
                //  即请求行无错，可以继续进行。故状态转换
                HTTP_CODE http_state = parse_requestline(text_line);
                if(http_state==NO_REQUEST)
                    ;
                    // m_check_state = CHECK_STATE_HEADER; //  状态转移。请求行检查完毕 来到请求头
                else if(http_state==BAD_REQUEST)
                    return BAD_REQUEST;     //  之后process_write 生成404报文
                break;
            }
            case CHECK_STATE_HEADER:
            {
                HTTP_CODE http_state = parse_header(text_line);
                if(http_state==GET_REQUEST) //  获得完整数据 则break，脱离状态机，不必再循环
                    return do_request();
                else if(http_state==NO_REQUEST) //  请求头解析完，报文仍然没有读全，确定是POST请求，状态转换。
                    ;
                    // m_check_state = CHECK_STATE_CONTENT;
                else if(http_state==BAD_REQUEST) //  之后process_write 生成404报文
                    return BAD_REQUEST;
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                HTTP_CODE http_state = parse_content(text_line);
                //  完整解析POST请求后，跳转到报文响应函数
                if(http_state==GET_REQUEST)
                    return do_request();
                if(http_state==BAD_REQUEST)
                    return BAD_REQUEST;
                //  解析完消息体即完成报文解析，避免再次进入循环，更新line_state
                line_state = LINE_OPEN;         //  这里到底为什么 跟 while第一个条件有关 解析一行就走了这就？？               
                break;
            }    
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}


//  weiwan
void http_conn::process()
{
    HTTP_CODE http_read_ret = this->process_read();   //  解析http请求
    if(http_read_ret == NO_REQUEST)
    {
        modfd(m_epfd,m_sockfd,EPOLLIN); //  因为oneshot了，这次又没读全，所以重新注册 等待下一次接收信息
        return ;
    }
    
    //  可优化：
    //   process完成报文响应（生成响应报文,完成文件映射，并将他们放入对应数组中）
    //  http_read_ret = BAD_REQUEST时，会生成404报文，而不会关闭连接。那么之前读入的这个read_buf缓冲区里的（准确说是m_checked_idx检查过的）
    //  都是错误的 没有用的。但是依然留在那里。会占用空间，浪费。后续读会接着m_read_buf+m_read_idx读取，当read_buf读满时，则会在read_conn函数中断开连接。
    //  因此，每次读到BAD_REQUEST时，直接断开连接。或者将错误数据删去。省得占用空间。
    bool http_write_ret = this->process_write(http_read_ret);
//  所以即使受到的报文格式出错也不会停止返回响应报文，只要返回响应状态马的报文即可。
//  所以只要成功生成报文（哪怕404），http_write_ret也是true
//  断开连接只是在发送完报文之后，由于m_linger = 0，故断开连接
    if(!http_write_ret)     //  响应报文生或者文件成失败
    {
        close_conn();
    }
    modfd(m_epfd,m_sockfd,EPOLLOUT);    //  重置一下
}

// 1. 当m_read_buf缓冲区过小，输入数据超过数组大小数据时
//      recv函数只会读取前BUFFER_SIZE个字符，最后BUFFER_SIZE-m_read_idx=0时，返回0。即r_len==0。故会在recv中return false。故会在驻县城中的read_conn调用返回处关闭连接。
// 2. 当输入的内容啥也不是时，read_conn 读取进入buf。ok，不返回false 。 状态机解析报文 BAD_REQUEST，但是此时并不关闭连接阿。
//  生成响应报文 准备文件：process_write（） 如果正常生成404报文，则ok。
//  那么准备完数据之后，接下来就是write。正常把404报文write给client。在这之后，由于m_linger = 0，故server主动正常关闭连接。
//  这就是一次正常的http请求+响应。如果请求报文符合语法，则变成生成正常报文+准备file映射即可。 并且请求报文中可能会有m_linger = true，即完成后不断开连接。
// 3. m_read_idx 作用：+
//  为了判断解析报文时到哪里为止是我们要解析的数据。
//  为了得出剩余缓冲区大小，好接着安全的读数据入buf，防止越界。（缓冲区不断消耗 但是缓冲区大小还按BUFFER_SIZE判断）为了判断缓冲区大小是否消耗完。消耗完则重新建立连接。
//  是为了接上上一次的数据，防止覆盖，
bool http_conn::read_conn()
{
    //  读满了 false 然后就会close这个对象（fd）然后该对象再次init时，就会重新设置m_read_idx = 0，故不会溢出
    if( m_read_idx >= READ_BUFFER_SIZE ) {   //  m_read_idx 下一次要读的起点，就是这里。缓冲区大小[0,BUFFER_SIZE-1]
        std::cout<<m_read_idx<<std::endl;
        return false;
    }
    //  ET模式下 循环读取sockfd上的数据 直至读完
    while(1)
    {
        int r_len = recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);  
        //不用-1 没有必要保留个位置用来'\0'。因为我们有m_read_idx来指示从哪里到哪里是[m_read_buf,m_readidx)我收到的报文
        //  BUFFER_SIZE - m_read_idx= 0 时 recv返回0
        if(r_len==-1)
        {
            // std::cout<<r_len<<" "<<m_read_idx<<" "<<READ_BUFFER_SIZE-m_read_idx<<std::endl;
            if(errno==EWOULDBLOCK || errno==EAGAIN) //  数据已经读完 重置
            {
                // modfd(m_epfd,m_sockfd,EPOLLIN); //  数据已经读完 重置
                break;  //  std::cout<<"read later"<<std::endl;
            }
            else 
            {
                LOG_ERROR("%s","read false");
                Log::get_instance()->flush();
                return false;   //  发生读错误
            }
        }
        else if(r_len==0)   //  对端关闭  或者 recv当缓冲区剩余长度变成0时。 即recv的长度参数为0
        {   //  然而我们注册了EPOLLRDHUP事件，那么 关闭事件就不会走到EPOLLIN这个。他先被EPOLLRDHUP判断拿走了，因此不会走到这里，在main函数里就处理了给
            //  所以这里只是应对缓冲区的剩余长度不够
            // std::cout<<r_len<<" "<<m_read_idx<<" "<<READ_BUFFER_SIZE-m_read_idx<<std::endl;
            return false;   //  我觉得不会 因为由EPOLLRDUP事件了已经 
            //  不过这里既然已经注册了
        }
        m_read_idx += r_len;
    }
    // printf("%d\t%d\n%s",m_epfd,m_read_idx,m_read_buf);
    // LOG_INFO("%d\t%d\n%s",m_epfd,m_read_idx,m_read_buf);
    // Log::get_instance()->flush();
    
    return true;
}

void removefd(int epfd,int fd)
{
    epoll_ctl(epfd,EPOLL_CTL_DEL,fd,NULL);  //  不再监听    
    close(fd);  //  关闭socket
}

void http_conn::close_conn()
{
    if(m_sockfd!=-1)
    {
        // std::cout<<m_epfd<<" closed"<<std::endl;
        //  其余像m_read_idx这样的变量 在该对象下一次init时就重置了。
        removefd(m_epfd,m_sockfd);    
        --m_user_cnt;
        m_sockfd=-1;
    }
}

int setnonblocking(int fd)
{
    int old_option = fcntl(fd,F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}



//  通常,我们根据recv返回值来判断socket是接收到的有效数据还是对方关闭连接的请求
//  但 EPOLLRHUP事件,在socke上接收到对方关闭连接的请求后触发.
void addfd(int epfd,int fd,bool et,bool oneshot)    //  lfd 可以ET么？
{
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLRDHUP;  // 读 ET 关闭
    if(et)
        event.events |= EPOLLET;
    event.data.fd = fd;
    if(oneshot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epfd,EPOLL_CTL_ADD,fd,&event);
    setnonblocking(fd);
}


void http_conn::init(int fd,const sockaddr_in& addr)
{
    //  socket初始化
    m_sockfd = fd;
    m_cliet_addr = addr;
    //  端口复用
    int reuse = 1;
    setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof reuse);
    //  监听本socket
    addfd(m_epfd,m_sockfd,true,true);
    //  ++连接数
    ++m_user_cnt;

    init();

    return ;
}


int http_conn::m_epfd = 0;
int http_conn::m_user_cnt = 0;
//  静态成员变量初始化
//1)在静态内存区中
//2)所有类对象共用这一个变量,只有唯一一个
//3)必须在类外面显示定义，显示定义的时候不加static
//4)可以通过类对象访问，也可以通过类名加作用域访问

