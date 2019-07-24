#include <iostream>
#include <thread>
#include <unistd.h>
#include <list>
#include <mutex>
#include <future>
#include <functional>
#include <atomic>
#include <condition_variable>
#pragma once
using namespace std;

//  线程池支持的任务基类，任务须由Run()函数实现；
class ThreadPool__Task{
    public:
        ThreadPool__Task(){}
        thread::id GetThreadID(){ return this_thread::get_id(); }
        virtual void Run() = 0;
};


//  线程池中的线程对象；
class WorkThread{
    public:
        WorkThread():_myTask_(nullptr),_isStop_(false){
            _isRunning_.store(true);                        //标记该线程为运行状态；
            _myThread_ = thread(&WorkThread::Run , this);   //创建线程池的线程；
        }
        ~WorkThread(){
            if( !_isStop_ ) Stop(); //停止线程任务；
            if( _myThread_.joinable() ) _myThread_.join();//线程回收；
        }
        WorkThread(const WorkThread & thread) = delete;
        WorkThread(const WorkThread && thread) = delete;
        WorkThread & operator=(const WorkThread & thread) = delete;
        WorkThread & operator=(const WorkThread && thread) = delete;
        bool Assign(ThreadPool__Task* task);//取得线程任务；
        void Stop();                        //暂停；
        void Notify();                      //通知；
        void Notify_all();                  //通知所有；
        bool IsExecuting();                 //判断线程是否正在执行任务；
        thread::id GetThreadId();           //取得线程号；
        virtual void Run();                 //任务执行接口；

   private:
        thread  _myThread_; //创建线程对象；
        ThreadPool__Task*  _myTask_;    //任务指针；
        mutex  _mutexThread_ , _mutexCondition_ , _mutexTask_;//线程锁、条件锁、任务锁；
        condition_variable  _my_condition_; //运行条件变量；
        atomic<bool>  _isRunning_;  //运行状态；
        bool  _isStop_;             //停止状态；
};

//  线程池的线程队列
//  主要功能：添加、返回、删除线程；暂停所有线程；动态线程池增减功能；
class IdleThreadList{
    public:
        IdleThreadList(const size_t counts){ _Assign(counts); }
        ~IdleThreadList(){
            while( !_threadList_.empty() ){
                WorkThread* tmp = _threadList_.front();
                _threadList_.pop_front();
                delete tmp;
            }
        }
        void Push(WorkThread* thread_ptr);  //添加线程至队尾；
        WorkThread* Top();                  //返回最前部线程；
        void Pop();                         //弹出最前部线程；
        size_t Size();                      //查询队列长度；
        void Stop();                        //停止队列全部任务；
        void DynamicList_Plus(const size_t &num);   //动态增加线程；
        void DynamicList_Minus(const size_t &num);  //动态缩减线程；

    private:
        list<WorkThread*> _threadList_;     //线程表；
        mutex _mutexThread_;                //线程锁；
        void _Assign(const size_t counts);
};

//  线程池对象
//  主要功能：1、开始、暂停、终止线程池中的线程；
//            2、视任务规模，动态控制线程池的大小；
class ThreadPool{
    public:
        ThreadPool(const size_t maxcount , const size_t mincount,
                const size_t counts , const size_t DN) : _isExit_(false){
            if(maxcount < mincount){ cout << "ERROR !\n\tThreadPool: maxcount < mincount" << endl; exit(1); } 
            _myThread_Counts_ = counts;
            _myThread_MaxNum_ = maxcount;
            _myThread_MinNum_ = mincount;
            _myThread_DN_ = DN;
            _myIsRunning_.store(true);
            _myIsEnd_.store(false);
            _myIdleThreadList_ = new IdleThreadList(_myThread_Counts_); //创建线程表；
            _myThread_ = thread(&ThreadPool::Run , this);               //空闲线程轮询并使其执行任务；
            _myThread_NumContral_ = thread(&ThreadPool::_DynamicThread , this); //创建线程监控线程池大小；
            Start();
        }
        ~ThreadPool(){
            cout << "~ThreadPool()  !!!" << endl;
            if( !_isExit_ ) Exit();
        }
        size_t ThreadCounts();  //返回线程数量；
        bool IsRunning();   //判断是否运行；
        void AddTask(ThreadPool__Task* task);//添加任务至任务队列；
        void Start();   //开始任务；
        void Stop();    //停止任务；
        void Exit();    //退出任务并回收线程；
        void Run();     //空闲线程轮询并使其执行任务；

    private:
        thread _myThread_;
        thread _myThread_NumContral_;
        IdleThreadList* _myIdleThreadList_;
        list<ThreadPool__Task*> _taskList_;
        atomic<bool> _myIsRunning_;
        atomic<bool> _myIsEnd_;
        atomic<size_t> _myThread_Counts_ , _myThread_MaxNum_ , _myThread_MinNum_,_myThread_DN_;
        condition_variable _condition_Task_,_condition_Running_;
        mutex _mutexTask_,_mutexThread_,_mutexRunning_;
        bool _isExit_;
        void _DynamicThread();
};


//----------------------------------------------------------------------//
//
//              *******   函数实现   *******
//

//  为线程取得具体任务；
bool WorkThread::Assign(ThreadPool__Task* task){
    _mutexTask_.lock();
    if( _myTask_ != nullptr ){
        _mutexTask_.unlock();
        return false;
    }
    _myTask_ = task;
    _mutexTask_.unlock();
    _my_condition_.notify_one();
    return true;
}
//  暂停该线程任务；
void WorkThread::Stop(){
    _isRunning_.store( false );
    _mutexThread_.lock();
    if(_myThread_.joinable()){
        _my_condition_.notify_all();
        _myThread_.join();
    }
    _mutexThread_.unlock();
    _isStop_ = true;
}
//  通知（用于通知线程执行新任务）；
void WorkThread::Notify(){
    _mutexCondition_.lock();
    _my_condition_.notify_one();
    _mutexCondition_.unlock();
}
void WorkThread::Notify_all(){
    _mutexCondition_.lock();
    _my_condition_.notify_all();
    _mutexCondition_.unlock();
}
//  判断是否正在执行；
bool WorkThread::IsExecuting(){
    _mutexTask_.lock();
    bool res = ( _myTask_ == nullptr );
    _mutexTask_.unlock();
    return !res;
}
//  取得线程号；
thread::id WorkThread::GetThreadId(){
    return this_thread::get_id();
}
//  执行任务；
void WorkThread::Run(){
    ThreadPool__Task* task = nullptr;
    while( true ){
        //当要求暂停时若没有任务则结束线程
        if(!_isRunning_.load()){
            _mutexTask_.lock();
            if( _myTask_ == nullptr ){
                _mutexTask_.unlock();
                break;
            }
            _mutexTask_.unlock();
        }
        //有任务则执行任务
        unique_lock<mutex> lock(_mutexTask_);
        _my_condition_.wait(lock,
                [this]{return !((_myTask_ == nullptr) && this->_isRunning_.load());} );
        task = _myTask_;
        _myTask_ = nullptr;
        if( task == nullptr )
            continue;
        task->Run();
        delete task;
        task = nullptr;
    }
}

//  添加线程至队尾；
void IdleThreadList::Push(WorkThread* thread_ptr){
    if( thread_ptr == nullptr )
        return;
    _mutexThread_.lock();
    _threadList_.push_back(thread_ptr);
    _mutexThread_.unlock();
}
//  返回最前部线程对象；
WorkThread* IdleThreadList::Top(){
    WorkThread* thread_ptr;
    _mutexThread_.lock();
    if( _threadList_.empty() )
        thread_ptr = nullptr;
    else
        thread_ptr = _threadList_.front();
    _mutexThread_.unlock();
    return thread_ptr;
}
//  弹出队尾线程
void IdleThreadList::Pop(){
    _mutexThread_.lock();
    if(!_threadList_.empty()){
        _threadList_.pop_front();
    }
    _mutexThread_.unlock();
}
//  返回线程队列大小；
size_t IdleThreadList::Size(){
    size_t counts = 0u;
    _mutexThread_.lock();
    counts = _threadList_.size();
    _mutexThread_.unlock();
    return counts;
}
//  停止所有线程的任务；
void IdleThreadList::Stop(){
    _mutexThread_.lock();
    for(auto thread_ptr : _threadList_)
        thread_ptr->Stop();
    _mutexThread_.unlock();
}
//  动态增加线程数量；
void IdleThreadList::DynamicList_Plus(const size_t &num){
    _mutexThread_.lock();
    for(size_t i=0u;i<num;i++)
        _threadList_.push_back(new WorkThread());
    _mutexThread_.unlock();
}
//  动态缩减线程数量；
void IdleThreadList::DynamicList_Minus(const size_t &num){
    _mutexThread_.lock();
    for(size_t i=num;i>0;i--){
        auto thread_ptr = _threadList_.front();
        auto list_it = _threadList_.begin();
        while( thread_ptr ->IsExecuting() ){
            _threadList_.pop_front();
            _threadList_.push_back(thread_ptr);
            thread_ptr = _threadList_.front();
            list_it ++;
        }
        _threadList_.erase(list_it);
    }
    _mutexThread_.unlock();
}
//  批量创建线程；
void IdleThreadList::_Assign(const size_t counts){
    for(size_t i=0u;i<counts;i++)
        _threadList_.push_back(new WorkThread());
}

//  返回线程数量；
size_t ThreadPool::ThreadCounts(){
    return _myThread_Counts_.load();
}
//  判断是否运行
bool ThreadPool::IsRunning(){
    return _myIsRunning_.load();
}
//  增加任务至任务列表；
void ThreadPool::AddTask(ThreadPool__Task* task){
    if( task == nullptr )
        return;
    _mutexTask_.lock();
    _taskList_.push_back(task);
    _mutexTask_.unlock();
    _condition_Task_.notify_one();
}
//  开启任务；
void ThreadPool::Start(){
    _myIsRunning_.store( true );
    _condition_Running_.notify_one();
}
//  停止任务；
void ThreadPool::Stop(){
    _myIsRunning_.store( false );
}
//  退出线程并回收；
void ThreadPool::Exit(){
    _myIsEnd_.store( true );
    _condition_Task_.notify_all();
    _mutexThread_.lock();
    if( _myThread_.joinable() )
        _myThread_.join();
    if( _myThread_NumContral_.joinable() )
        _myThread_NumContral_.join();
    _mutexThread_.unlock();
    _myIdleThreadList_->Stop();
    _isExit_ = true;
}
//空闲线程轮询并使空闲线程执行任务；
void ThreadPool::Run(){
    WorkThread* thread_ptr = nullptr;
    ThreadPool__Task* task = nullptr;
    while( true ){
        if( _myIsEnd_.load() ){
            break;
        } else {
            _mutexTask_.lock();
            if( _taskList_.empty() ){
                _mutexTask_.unlock();
                continue;
            }
            _mutexTask_.unlock();
        }

        {  //  block
            unique_lock<mutex> lockRunning(_mutexRunning_);
            _condition_Running_.wait(lockRunning,
                    [this]{return this->_myIsRunning_.load();});
        }

        thread_ptr = nullptr;
        task = nullptr;

        {  //  block
            unique_lock<mutex> lock(_mutexTask_);
            _condition_Task_.wait(lock,
                    [this]{return !(this->_taskList_.empty() && !this->_myIsEnd_.load());});
            if( !_taskList_.empty() ){
                task = _taskList_.front();
                _taskList_.pop_front();
            }
        }

        do{
            thread_ptr = _myIdleThreadList_ ->Top();
            _myIdleThreadList_ ->Pop();
            _myIdleThreadList_ ->Push(thread_ptr);
        } while( thread_ptr ->IsExecuting() );

        thread_ptr ->Assign(task);
        thread_ptr ->Notify();
    }
}
//  根据任务规模，动态调整线程池的大小；
void ThreadPool::_DynamicThread(){
    size_t taskList_Size = _taskList_.size();
    size_t idleThreadList_Size = _myIdleThreadList_ ->Size();
    while( !_myIsEnd_.load() ){
        sleep(3);
        taskList_Size = _taskList_.size();
        idleThreadList_Size = _myIdleThreadList_ ->Size();
        if( taskList_Size == 0 || taskList_Size < idleThreadList_Size){
            if(_myThread_Counts_.load() - _myThread_DN_ < _myThread_MinNum_.load()){
                continue;
            } else {
                _myThread_Counts_ -= _myThread_DN_;
                _myIdleThreadList_ -> DynamicList_Minus(_myThread_DN_);
                continue;
            }
        } 
        if( taskList_Size > idleThreadList_Size * 10){
            if( _myThread_Counts_.load() + _myThread_DN_ > _myThread_MaxNum_.load() ){
                continue;
            } else {
                _myThread_Counts_ += _myThread_DN_;
                _myIdleThreadList_ -> DynamicList_Plus(_myThread_DN_);
                continue;
            }
        }
    }
}

