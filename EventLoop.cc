#include "EventLoop.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <memory>

#include "Channel.h"
#include "EPollPoller.h"
#include "Logger.h"
#include "Poller.h"

// 防止一个线程创建多个EventLoop
__thread EventLoop *t_loopInThisThread = nullptr;

// 定义默认的Poller I/O复用接口的超时时间
const int kPollTimeMs = 10000;

// muduo不是用一个线程安全队列，mainreactor生成channel到队列，subreactor从队列取channel消费
// 而是每个loop通过创建wakeupfd，用来notify唤醒subreactor处理新的channel
int createEventFd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)
    {
        LOG_FATAL("eventfd error:%d\n", errno);
    }
    return evtfd;
}

EventLoop::EventLoop()
    : looping_(false),
      quit_(false),
      callingPendingFunctors_(false),
      threadId_(CurrentThread::tid()),
      poller_(Poller::newDefaultPoller(this)),
      wakeupFd_(createEventFd()),
      wakeupChannel_(new Channel(this, wakeupFd_))
{
    LOG_DEBUG("EventLoop created %p in thread %d\n", this, threadId_);
    if (t_loopInThisThread)
    {
        LOG_FATAL("Another EventLoop %p exists in this thread %d\n", this, threadId_);
    }
    else
    {
        t_loopInThisThread = this;
    }

    // 设置wakeupFd_的事件类型以及发生时间以后的回调操作
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    // 每一个EventLoop都将监听wakeupChannel_的EPOLLIN读事件
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

// 开启事件循环
void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;

    LOG_INFO("EventLoop %p start loop\n", this);

    while (!quit_)
    {
        activeChannels_.clear();
        // epoll_wait 会监听到两种fd，一种是用户client fd，一种是各个loop之间的wakeup fd
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
        for (Channel *channel : activeChannels_)
        {
            // Poller监听哪些channel发生事件了，然后上报给EventLoop来通知channel处理相应的事件
            channel->handleEvent(pollReturnTime_);
        }
        // 执行当前EventLoop事件循环需要处理的回调操作
        doPendingFunctors();
    }
    LOG_INFO("EventLoop %p stop looping\n", this);
    looping_ = false;
}

/**
 * 退出事件循环
 * 1. loop在当前线程调用quit()
 * 2. 在非loop的线程中调用loop的quit()
 */
void EventLoop::quit()
{
    quit_ = true;

    // 如果是在其它线程中调用的quit(), 如在一个subloop调用了mainloop线程的quit, 因为已经将quit_置为true
    // 那么mainloop会被唤醒(让epoll_wait返回)后的下次while循环会退出从而退出事件循环
    if (!isInLoopThread())
    {
        wakeup();
    }
}

// 在当前loop中执行cb
void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread())  // 在当前的loop线程中执行cb
    {
        cb();
    }
    else  // 在非当前的loop线程中执行cb，就需要唤醒loop所在线程执行cb
    {
        queueInLoop(cb);
    }
}

// 把cb放入到队列中，唤醒loop所在的线程，执行cb
void EventLoop::queueInLoop(Functor cb)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(cb);
    }

    // 唤醒相应的需要执行上面回调操作的loop的线程
    // || callingPendingFunctors_ 是应对当前loop正在执行回调，但是loop又有了新的回调，需要让新的回调得到执行
    if (!isInLoopThread() || callingPendingFunctors_)
    {
        wakeup();  // 唤醒loop所在线程
    }
}

void EventLoop::handleRead()
{
    uint64_t one = 1;
    ssize_t n = read(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop::handleRead() reads %ldbytes instead of 8", n);
    }
}

// 唤醒loop所在的线程
// 向wakeupFd_写一个数据，wakeupChannel就会发生读事件，当前loop线程就会从poll()阻塞中被唤醒
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop::handleRead() writes %ldbytes instead of 8", n);
    }
}

// 执行回调
void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // 通过一个局部变量functors与pendingFunctors_交换来解放pendingFunctors_
        // 不影响mainloop继续向pendingFunctors_放入回调
        functors.swap(pendingFunctors_);
    }

    for (const Functor &functor : functors)
    {
        functor();  // 执行当前loop需要执行的回调操作
    }
    callingPendingFunctors_ = false;
}

// Poller的方法
void EventLoop::updateChannel(Channel *channel)
{
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel)
{
    poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel *channel)
{
    poller_->hasChannel(channel);
}
