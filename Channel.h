#pragma once

#include <functional>
#include <memory>

#include "Timestamp.h"
#include "noncopyable.h"

class EventLoop;

/**
 * Channel 理解为通道，封装了sockfd和其他感兴趣的event，如EPOLLIN，EPOLLOUT事件
 * 还绑定了poller返回的具体事件  
 */
class Channel : noncopyable
{
public:
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp)>;

    Channel(EventLoop *loop, int fd);
    ~Channel();

    // fd得到Poller通知以后，处理事件的回调方法
    void handleEvent(Timestamp receiveTime);

    // 设置回调函数对象
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 防止channel被手动remove掉，channel还在执行回调操作
    void tie(const std::shared_ptr<void> &);

    int fd() const { return fd_; }
    int events() const { return events_; }
    void set_revents(int revt) { revents_ = revt; }

    // 设置fd相应的事件状态
    void enableReading()
    {
        events_ |= KReadEvent;
        update();
    }
    void disableReading()
    {
        events_ &= ~KReadEvent;
        update();
    }
    void enableWriting()
    {
        events_ |= KWriteEvent;
        update();
    }
    void disableWriting()
    {
        events_ &= ~KWriteEvent;
        update();
    }
    void disableAll()
    {
        events_ = KNoneEvent;
        update();
    }

    // 返回fd当前的事件状态
    bool isNoneEvent() const { return events_ == KNoneEvent; }
    bool isReadEvent() const { return events_ == KReadEvent; }
    bool isWriteEvent() const { return events_ == KWriteEvent; }

    int index() { return index_; }
    void set_index(int index) { index_ = index; }

    // one loop per thread
    EventLoop *ownerLoop() { return loop_; }
    void remove();

private:
    void update();
    void handleEventWithGuard(Timestamp receiveTime);

    // 对感兴趣事件的状态的描述
    static const int KNoneEvent;
    static const int KReadEvent;
    static const int KWriteEvent;

    EventLoop *loop_;  // 事件循环
    const int fd_;     // fd，Poller所监听的对象
    int events_;       // 注册 fd 感兴趣的事件
    int revents_;      // Poller返回的具体发生的事件
    int index_;        // 该channel在poller的状态(未添加/已添加/已删除)

    std::weak_ptr<void> tie_;  // 跨线程的对象生存状态的监听
    bool tied_;

    // 因为Channel通道里面能够获知fd最终发生的具体事件revents，所以它负责调用具体事件的回调操作
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};
