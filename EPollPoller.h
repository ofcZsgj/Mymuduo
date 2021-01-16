#pragma once

#include <sys/epoll.h>

#include <vector>

#include "Poller.h"

class EPollPoller : public Poller
{
public:
    EPollPoller(EventLoop* loop);
    ~EPollPoller() override;

    // 重写基类Poller的抽象方法
    Timestamp poll(int timeoutMs, ChannelList* artiveChannels) override;
    void updateChannel(Channel* channel) override;
    void removeChannel(Channel* channel) override;

private:
    static const int KInitEventListSize = 16;  // 存放epoll_event的vector初始长度为16

    // 记录有事件发生的channels，用于返回给EventLoop
    void fillActiveChannels(int numEvents, ChannelList* activeChannels);
    // 更新channel通道
    void update(int operation, Channel* channel);

    using EventList = std::vector<epoll_event>;

    int epollfd_;
    EventList events_;
};