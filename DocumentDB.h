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
#include "threadpool.h"
#include "mysql.h"

//定义心跳检测 避免服务器误读；
#define HEARTBEAT "HEARTBEAT"
#define HEARTBEATSIZE 9
#define MAXLINE 4096
//定义 MySQL 的登陆信息；
#define USERNAME    "***"
#define PASSWORD    "***"
#define HOST        "***"
#define TABLE       "***"
#define MYSQLPORT   3306

#pragma comment(lib,"libmysql.lib")
#pragma once
using namespace std;

//  MySQL 类，用于实现连接数据库的基本工作；
class MySQL_Root{
    public:
        MySQL_Root(){ mysql_init(&_myCon_); _UserPlugin(); }//初始化和保存登陆信息；
        virtual ~MySQL_Root(){}
        string result;  //用于保存传给套接字的结果；

    protected:
        char* _user_ ;      //用户名；
        char* _pswd_ ;      //密码；
        char* _host_ ;      //主机名；
        char* _table_;      //表名；
        unsigned int _port_;//端口；
        MYSQL _myCon_;
        MYSQL_ROW _row_;
        string _commond_;   //用于保存发给MySQL的命令；
        void _UserPlugin(); //登录函数；
        void _MySQLConnect();//连接函数；
};

//  定义基本的操作
//  包括：按年查找、按作者查找等
class Normal_Operator{
    public:
        Normal_Operator(){}
        virtual ~Normal_Operator(){}
        virtual void SearchByYear(string year) = 0;     //按年查找；
        virtual void SearchByAuther(string Auther) = 0; //按作者查找；
        virtual void ShowAll() = 0;                     //显示全部数据；
};

//  数据库具体操作的实现
class Database_Operator : public Normal_Operator ,public MySQL_Root {
    public:
        Database_Operator() : MySQL_Root(){}
        virtual ~Database_Operator(){}
        void SearchByYear(string Year);     //按年查找；
        void SearchByAuther(string Auther); //按作者查找；
        void ShowAll();                     //显示全部数据；

    protected:
        void _RunCommond(int ResRowNum);    //执行具体命令；
};

//  线程池任务对象，用于实现具体的响应操作
//  主要功能包括：1、读取信息并执行；2、返回执行结果；
class ServerTask : public ThreadPool__Task , public Database_Operator{
    public:
        ServerTask( const int &cfd , const struct sockaddr_in &ca , map<int,int>* heartCountMap);
        virtual ~ServerTask(){}
        void Run();     //该函数对应线程池执行接口；
    private:
        int _recvStatus_ , _sendStatus_;    //收发状态；
        int _confd_;                        //客户端信息；
        char _ipstr_[128] , _buf_[MAXLINE]; //socket信息存储空间；
        struct sockaddr_in _clientAddr_;    //客户端；
        map<int,int>* _heartCount_;         //心跳检测对象表；
        char* _heartChar_;                  //心跳检测密码；

        void _CommondAnalyse();             //分析客户端 需求代码，格式：  “查询信息#查询属性”；
        bool _Send();                       //发送函数；
        bool _Receive();                    //接收函数（执行客户端需求）；
};

//-------------------------------------------------------------------------------
//
//                   *******      函数实现      *******
//

//  MySQL登录信息存储；
void MySQL_Root::_UserPlugin(){
    _user_   =   USERNAME;
    _pswd_   =   PASSWORD;
    _host_   =   HOST;
    _table_  =   TABLE;
    _port_   =   MYSQLPORT;
}
//  连接MySQL;
void MySQL_Root::_MySQLConnect(){
    if(mysql_real_connect(&_myCon_ , _host_ , _user_ , _pswd_ , _table_ , _port_ , NULL , 0))
        cout << "mySQL connect succeed !" << endl;
    else{
        cout << "ERROR !\n\tMySQL Connect Failed !" << endl;
        exit(1);
    }
}

//  执行客户端需求的命令（存储于 MySQL_Root :: _commond_ 中）；
void Database_Operator::_RunCommond(int ResRowNum){
    result = "";        //结果清零
    _MySQLConnect();    //连接至数据库
    //进行操作请求
    if(mysql_real_query( &_myCon_ , _commond_.data() , (unsigned long) _commond_.length() ) ){
        cout << "mysql_real_query failure : " << _commond_  << endl;
        result =  "mysql_real_query failure " ;  
        return;
    }
    //保存请求结果；
    MYSQL_RES * _res_ = mysql_store_result( &_myCon_ );
    if( _res_ == NULL ){
        cout << "Result is NULL !" << endl;
        result =  "Result is NULL !";
        mysql_free_result(_res_);
        return;
    }
    else{
        //存储结果至 result 中；
        while( _row_ = mysql_fetch_row( _res_ ) ){
            for(int field = 0;field<ResRowNum;field++){
                result += _row_[field] ;
                result += " | " ;
            }
            result += "\n" ;
        }
        mysql_free_result(_res_);
    }
    //断开数据库连接；
    mysql_close(&_myCon_);
}

//  按年查找；
void Database_Operator::SearchByYear(string Year){
    char* yearChar;
    _commond_ = "CALL SearchByYear(" + Year + ");";
    _RunCommond(2);
}

//  按作者查找；
void Database_Operator::SearchByAuther(string Auther){
    _commond_ = "CALL SearchByAuther(\"" + Auther + "\");";
    _RunCommond(2);
}

//  显示全部数据；
void Database_Operator::ShowAll(){
    _commond_ = "SELECT Year,Auther,Title FROM test ORDER BY Year;";
    _RunCommond(3);
}


//  登记线程池分配任务的对象信息
ServerTask::ServerTask( const int &cfd , const struct sockaddr_in &ca , map<int,int>* heartCountMap){
    _confd_ = cfd;
    _clientAddr_ = ca;
    _heartCount_ = heartCountMap;
    _heartChar_ = HEARTBEAT;
}

//  执行 线程池 分配的任务；
void ServerTask::Run(){
    while(true){
        if( _Receive() != true)
            break;
        if( _Send() != true )
            cout << "SendBack ERROR !!! " << endl;
    }
    close( _confd_ );
}

//  发送结果至客户端；
bool ServerTask::_Send(){
    bzero( &_buf_ , sizeof(_buf_) );
    result.copy(_buf_,result.size(),0);
    result = "";
    _sendStatus_ = send(_confd_ , _buf_ , strlen(_buf_) , 0 ); 
    if( _sendStatus_ <= 0){
        bzero( &_buf_ , sizeof(_buf_) );
        return false;
    }
    else{
        bzero( &_buf_ , sizeof(_buf_) );
        return true;
    }
}

//  对客户端需求进行分析并执行    需求格式：  “查询信息#查询属性”；
void ServerTask::_CommondAnalyse(){
    char* buf_ptr = _buf_;
    char* separation_commond = strchr(_buf_,'#');
    string operatorCode = ( separation_commond + 1);
    int operatorNum = stoi(operatorCode );
    string parameter;
    while(buf_ptr != separation_commond){
        parameter += *(buf_ptr ++);
    }

    //  执行具体需求；
    switch(operatorNum){
        case 0:{
                   ShowAll();
                   break;
               }
        case 1:{
                   SearchByYear(parameter);
                   break;
               }
        case 2:{
                   SearchByAuther(parameter);
                   break;
               }
        default:{
                   result = "WRONG OPTION";
                   break;
               }
    };
}

//  接收客户端请求并执行
bool ServerTask::_Receive(){
    while(true){
        _recvStatus_ = recv(_confd_ , _buf_ , MAXLINE , 0 );
        if( _recvStatus_ < 0 ){
            return false;
        } else if(_recvStatus_ == 0){
            return false;
        } 
        if( _heartCount_ -> count( _confd_ ) != 0 ){
            _heartCount_->find( _confd_  ) ->second = 0;
        }
        else{
            return false;
        }
        //  若为心跳检测则忽略；
        if( strncmp( _buf_ , _heartChar_ , HEARTBEATSIZE) == 0 ){
            bzero( &_buf_ , sizeof(_buf_) );
            continue;
        } else {    //为需求则执行；
            CommondAnalyse();                   //分析请求并执行；
            bzero( &_buf_ , sizeof(_buf_) );
            return true;
        }
    }
}



