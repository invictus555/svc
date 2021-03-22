//
//  SyncQueue.hpp
//  svc
//
//  Created by Asterisk on 3/19/21.
//

#ifndef SyncQueue_hpp
#define SyncQueue_hpp
#include <stdio.h>
#include <list>
#include <mutex>
#include <iostream>
#include <condition_variable>
using namespace std;

template <typename T>
class SyncQueue {
public:
    SyncQueue(int maxSize):maxSize_(maxSize), stop_(false) {}
    
    ~SyncQueue() {}
    
    void put(T&& t) {
        std::unique_lock<std::mutex> locker(mutex_);
        notFull_.wait(locker, [this]{
            auto full = dataQueue_.size() >= maxSize_;
            return stop_ || !full;
        });
        
        if (stop_) {
            return;
        }
        
        dataQueue_.push_back(std::forward<T>(t));
        notEmpty_.notify_one();
    }
    
    void put(const T& t) {
        std::unique_lock<std::mutex> locker(mutex_);
        notFull_.wait(locker, [this]{
            auto full = dataQueue_.size() >= maxSize_;
            return stop_ || !full;
        });

        if (stop_) {
            return;
        }
        
        dataQueue_.push_back(std::move<T>(t));
        notEmpty_.notify_one();
    }
    
    void front(T &t) {
        std::unique_lock<std::mutex> locker(mutex_);
        notEmpty_.wait(locker, [this]{
            return stop_ || !dataQueue_.empty();
        });
        
        if (stop_) {
            return;
        }
        
        t = dataQueue_.front();
        dataQueue_.pop_front();
        notFull_.notify_one();
    }
    
    void interrupt () { // 调用此函数可能会导致队列中的数据没被取走就被中止--->内存泄漏
        {
            std::unique_lock<std::mutex> locker(mutex_);
            stop_ = true;
        }
        
        notFull_.notify_all();
        notEmpty_.notify_all();
    }
    
    bool empty() {
        std::unique_lock<std::mutex> locker(mutex_);
        return dataQueue_.empty();
    }
    
    bool full() {
        std::unique_lock<std::mutex> locker(mutex_);
        return dataQueue_.size() == maxSize_;
    }
    
    size_t size() {
        std::unique_lock<std::mutex> locker(mutex_);
        return dataQueue_.size();
    }
    
private:
    bool stop_;
    int maxSize_;
    std::mutex mutex_;
    std::list<T> dataQueue_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    
};
#endif /* SyncQueue_hpp */
