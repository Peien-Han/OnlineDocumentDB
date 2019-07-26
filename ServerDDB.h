//***********************************************************************
//
//  ServerDDB.h：
//      1、定义 服务器所需的基本功能                ：class Server;
//      2、定义并实现 基于IPV4和支持TCP的服务器对象 ：class Server_IPV4_TCP;
//      3、定义并实现 文献检索服务器                ：template<class OnlineServer>
//                                                    class Server_DDB;
//
//  功能特点：
//      1、支持应用层级的 心跳检测，保证连接的有效性和资源分配的合理性
//      2、使用线程池进行客户端并发响应，提高处理效率和信息吞吐量
//
//  制作信息：
//      韩佩恩  2019 于 上海同济大学；
//
//  致谢：
//      感谢 CSDN 和 博客园 两个论坛上探讨技术和答疑解惑的热情网友！
//
//***********************************************************************

#if!defined SERVERDDB_H
#define SERVERDDB_H

#include <iostream>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <map>
#include "ThreadPool.h" //  线程池对象

#pragma once
using namespace std;

class Server{
    public:
        Server(){}
        virtual ~Server(){}
        virtual void _Initial() = 0;    //socket初始化；
        virtual void _Bind() = 0;       //socket端口绑定；
        virtual void _Listen() = 0;     //开启监听；
        virtual void _TaskHandle() = 0; //进行并发任务处理；
    protected:
        int _serverPort_;               //服务器段口号；
        int _listenNum_;                //允许接入的最大监听数量；
        int _socket_fd_ ,  _confd_;     //套接字返回值；
        struct sockaddr_in _serverAddr_ , _clientAddr_; //套接字地址；
};


class Server_IPV4_TCP : public Server{
    public:
        //输入 服务器端口号、最大监听数量、线程池线程数量上下限、初始线程数量、线程池动态变化步长；
        Server_IPV4_TCP( int serverPort , int listenNum ,
                int threadNumMax , int threadNumMin ,int threadNumInitial , int threadNumDn );
        virtual ~Server_IPV4_TCP();

        //  Socket 心跳检测
        virtual void HeartBeat();               //心跳检测主函数；
        virtual void HeartBeat_ADD(int client); //输入客户端监听值，将某次连接加入心跳检测集和；

    protected:
        ThreadPool* _pool_;                     //线程池指针；
        map<int,int> *_heartCount_;             //心跳检测监控的连接集和；

        void _Initial();            //socket初始化实现；
        void _Bind();               //socket端口绑定实现；
        void _Listen();             //监听实现；

        //线程池相关命令
        void _ThreadPool_Exit();    //线程池退出；
        void _TaskHandle_Stop();    //线程池全部暂停；
        void _TaskHandle_Start();   //线程池启动命令（可从暂停中启动）；
        virtual void _TaskHandle() = 0;
};


template <class OnlineService>
//OnlineService 为实现应答的具体实现对象（需继承threadpool.h中的ThreadPool__Task类，支持多线程）；
class Server_DDB : public Server_IPV4_TCP{
    public:
        //继承自Server_IPV4_TCP，强制需求输入 服务器端口号、最大监听数量；
        //可缺省：线程池线程数量上下限、初始线程数量、线程池动态变化步长；
        Server_DDB( int serverPort , int listenNum = 100,
                int threadNumMax = 300 ,int threadNumMin = 5 ,
                int threadNumInitial = 50 ,int threadNumDn = 5 ) : Server_IPV4_TCP(serverPort,listenNum,
                    threadNumMax,threadNumMin,threadNumInitial,threadNumDn)
        {
            _TaskHandle();  //对象构造后直接进入 客户端响应流程；
        }
        virtual ~Server_DDB(){}
    protected:
        virtual void _TaskHandle(); //客户端响应流程：接收客户端指令，添加任务池，添加心跳检测对象；
};


//--------------------------------------------------------------------------------------
//                                                                                    // 
//                          *******    函数实现  *******                              //

//  Server_IPV4_TCP 构造函数
//  主要功能：1、Socket初始化和端口绑定；2、线程池创建；3、心跳检测集和创建；4、开始监听；
Server_IPV4_TCP::Server_IPV4_TCP( int serverPort , int listenNum ,
        int threadNumMax , int threadNumMin ,int threadNumInitial , int threadNumDn ){
    _serverPort_ = serverPort;
    _listenNum_ = listenNum;
    _Initial();     //Socket初始化；
    _Bind();        //Socket端口绑定；
    _pool_ = new ThreadPool(threadNumMax,threadNumMin,threadNumInitial,threadNumDn); //线程池创建；
    _heartCount_ = new map<int,int>;    //心跳检测集和创建；
    _Listen();      //开始监听；
}

//  Server_IPV4_TCP 析构函数
//  主要功能：1、退出并删除线程池；2、删除心跳检测集和；
Server_IPV4_TCP::~Server_IPV4_TCP(){
    _ThreadPool_Exit(); 
    delete _pool_;
    _pool_ = nullptr;
    delete _heartCount_;
}

//  心跳检测主函数
//  每sleepTime秒轮询一次监控队列，当某连接长时间静止时主动断开连接并取消该监控；
//  当服务器收到有效指令或心跳（密码：HEARTBEAT）时，刷新监控值，否则监控值轮询时递增直至临界；
void Server_IPV4_TCP::HeartBeat(){
    int sleepTime = 5;      //轮询时间间隔（单位：秒）；
    int maxCount = 4;       //监控阈值，超出时判定监控对象为 “无效连接” ；
    while(true){
        //当监控对象的时候进行定时休眠；
        if(_heartCount_->empty()){
            sleep(sleepTime);
            continue;
        }
        //对监控对象进行轮询；
        for(auto it = _heartCount_->begin();it != _heartCount_->end();it++){
            if( (*it).second >= maxCount ){
                close( (*it).first );   //关闭超时对象的连接，释放Recv等阻塞；
                _heartCount_->erase(it);//取消该对象监控；
                continue;
            } else {
                (*it).second ++;    //监控值递增；
            }
        }
        sleep(sleepTime);   //睡眠SleepTime秒；
    }
}
//  添加监控对象，若对象已存在，则刷新；
void Server_IPV4_TCP::HeartBeat_ADD(int client){
    if( _heartCount_ -> count(client) != 0){
        _heartCount_ ->find(client)->second = 0;
    } else {
        _heartCount_ -> insert( pair<int,int>(client,0) );
    }
}

//  初始化Socket；
void Server_IPV4_TCP::_Initial(){
    if( ( _socket_fd_ = socket( AF_INET , SOCK_STREAM , 0 ) ) == -1){
        cout << "ERROR !\n\tServer Socket: Create socket error !!!" << endl;;
        exit(1);
    }
}

//  Socket绑定；
void Server_IPV4_TCP::_Bind(){
    bzero( &_serverAddr_ , sizeof( _serverAddr_ ) );
    _serverAddr_.sin_family = AF_INET;    // set IPV4
    _serverAddr_.sin_addr.s_addr = INADDR_ANY;
    _serverAddr_.sin_port = htons( _serverPort_ );
    if( bind( _socket_fd_ , (struct sockaddr *)&_serverAddr_ , sizeof(_serverAddr_) ) == -1){
        cout << "ERROR !\n\tServer Socket:  Bind failed !!!" << endl;
        exit(1);
    }
}

//  进行监听；
void Server_IPV4_TCP::_Listen(){
    listen( _socket_fd_ , _listenNum_ );
}

//  退出线程池；
void Server_IPV4_TCP::_ThreadPool_Exit(){
    _pool_ ->Exit();
    delete _pool_;
    _pool_ = nullptr;
};

//  暂停线程池；
void Server_IPV4_TCP::_TaskHandle_Stop(){
    _pool_ ->Stop();
}

//  开启线程池（可从stop后开启；
void Server_IPV4_TCP::_TaskHandle_Start(){
    _pool_ ->Start();
}

//  接收有效需求，创建心跳检测线程，将需求响应添加之任务池（并自动由线程池执行）；
template <class OnlineService>
void Server_DDB<OnlineService>::_TaskHandle(){
    unsigned int _addrLen_;
    thread HeartBeatThread(&Server_IPV4_TCP::HeartBeat,this);   //创建心跳检测线程；

    //持续进行检测
    while( true ){
        _addrLen_ = sizeof( _clientAddr_ );
        if( ( _confd_ = accept( _socket_fd_ , (struct sockaddr*)&_clientAddr_ , &_addrLen_) ) == -1){
            continue;
        }
        HeartBeat_ADD( _confd_ );   //添加心跳检测对象；
        _pool_ ->AddTask( new OnlineService( _confd_ , _clientAddr_ , _heartCount_) );//添加任务池；
    }

    close( _socket_fd_ );   //关闭服务器Socket；
    if(HeartBeatThread.joinable())
        HeartBeatThread.join(); //回收心跳检测线程；
}
#endif
