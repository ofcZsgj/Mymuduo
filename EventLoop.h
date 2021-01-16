#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "CurrentThread.h"
#include "Timestamp.h"
#include "noncopyable.h"

class Channel;
class Poller;

/**
 * 事件循环类 主要包含了两个大模块 Channel 和 Poller(epoll的抽象)
 */
class EventLoop : noncopyable
{
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    void loop();  // 开启事件循环
    void quit();  // 退出事件循环

    Timestamp pollReturnTime() const { return pollReturnTime_; }

    void runInLoop(Functor cb);    // 在当前loop中执行cb
    void queueInLoop(Functor cb);  // 把cb放入到队列中，唤醒loop所在的线程，执行cb

    void wakeup();  // 唤醒loop所在的线程

    // Poller的方法
    void updateChannel(Channel *channel);
    void removeChannel(Channel *channel);
    bool hasChannel(Channel *channel);

    // 判断EventLoop对象是否在当前线程里面
    bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }

private:
    void handleRead();         // wake up
    void doPendingFunctors();  // 执行回调

    using ChannelList = std::vector<Channel *>;

    std::atomic_bool looping_;
    std::atomic_bool quit_;  // 标志退出loop循环

    const pid_t threadId_;  // 记录当前loop所在的线程id

    Timestamp pollReturnTime_;  // poller返回事件的channel的时间点
    std::unique_ptr<Poller> poller_;

    // 当mainloop获取到一个新的channel，通过轮询算法选择一个subloop，通过该成员唤醒subloop处理channel
    int wakeupFd_;
    std::unique_ptr<Channel> wakeupChannel_;

    ChannelList activeChannels_;

    std::atomic_bool callingPendingFunctors_;  // 标识当前loop是否有需要执行的回调操作
    std::vector<Functor> pendingFunctors_;     // 存储loop需要执行的所有回调操作
    std::mutex mutex_;                         // 保护pendingFunctors_的线程安全
};
