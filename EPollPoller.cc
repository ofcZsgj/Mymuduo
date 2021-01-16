#include "EPollPoller.h"

#include <strings.h>
#include <unistd.h>

#include "Channel.h"
#include "Logger.h"
#include "errno.h"

// channel未添加到poller中
const int kNew = -1;  // channel的成员变量index_初始化为-1
// channel已添加到poller中
const int kAdded = 1;
// channel从poller中删除
const int kDeleted = 2;

EPollPoller::EPollPoller(EventLoop* loop)
    : Poller(loop),
      epollfd_(::epoll_create1(EPOLL_CLOEXEC)),  // epoll_create1 进程被替换时会关闭文件描述符。
      events_(KInitEventListSize)                // 初始化vector<epoll_event>
{
    if (epollfd_ < 0)
    {
        LOG_FATAL("epoll_create error:%d\n", errno);
    }
}

EPollPoller::~EPollPoller()
{
    ::close(epollfd_);
}

// epoll_wait
Timestamp EPollPoller::poll(int timeoutMs, ChannelList* artiveChannels)
{
    LOG_INFO("func=%s -> fd total count:%lu\n", __FUNCTION__, channels_.size());

    int numEvents = ::epoll_wait(epollfd_,
                                 &*events_.begin(),  // 容器第一个元素的地址
                                 static_cast<int>(events_.size()),
                                 timeoutMs);
    int savedErrno = errno;  // 考虑到多线程，先记录下来全局的errno
    Timestamp now(Timestamp::now());
    if (numEvents > 0)
    {
        LOG_INFO("%d events happend\n", numEvents);
        fillActiveChannels(numEvents, artiveChannels);
        if (numEvents == events_.size())
        {
            events_.resize(events_.size() * 2);
        }
    }
    else if (numEvents == 0)
    {
        LOG_DEBUG("%s timeout\n", __FUNCTION__);
    }
    else
    {
        if (savedErrno != EINTR)  // 如果不是系统调用被中断，说明是程序出现问题
        {
            errno = savedErrno;  // 因为从定义savedErrno到此处errno可能已经改变
            LOG_ERROR("EPollPoller::poll() err\n");
        }
    }
    return now;
}

// epoll_ctl
void EPollPoller::updateChannel(Channel* channel)
{
    const int index = channel->index();
    int fd = channel->fd();
    LOG_INFO("func:%s -> fd=%d events=%d index=%d \n", __FUNCTION__, fd, channel->events(), index);

    if (index == kNew || index == kDeleted)
    {
        if (index == kNew)
        {
            // 将 fd - channel 加入到map中
            channels_[fd] = channel;
        }
        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel);
    }
    else
    {
        if (channel->isNoneEvent())  // 如果channel对所有事件都不感兴趣
        {
            update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted);
        }
        else
        {
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

// 从poller中删除channel
void EPollPoller::removeChannel(Channel* channel)
{
    int fd = channel->fd();
    channels_.erase(fd);
    LOG_INFO("func:%s -> fd=%d \n", __FUNCTION__, fd);

    int index = channel->index();
    if (index == kAdded)
    {
        update(EPOLL_CTL_DEL, channel);
    }
    channel->set_index(kNew);
}

// 记录有事件发生的channels，用于返回给EventLoop
void EPollPoller::fillActiveChannels(int numEvents, ChannelList* activeChannels)
{
    for (int i = 0; i < numEvents; ++i)
    {
        Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        // EventLoop由此拿到了它的poller给它返回的所有发生事件的channel列表
        activeChannels->push_back(channel);
    }
}

// 更新channel通道
void EPollPoller::update(int operation, Channel* channel)
{
    epoll_event event;
    bzero(&event, sizeof event);
    int fd = channel->fd();

    event.events = channel->events();
    event.data.ptr = channel;
    event.data.fd = fd;

    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if (operation == EPOLL_CTL_DEL)
        {
            LOG_ERROR("epoll_ctl del err %d\n", errno);
        }
        else
        {
            LOG_FATAL("epoll_ctl add/mod err %d\n", errno);
        }
    }
}