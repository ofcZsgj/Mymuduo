#pragma once

#include <unordered_map>
#include <vector>

#include "Timestamp.h"
#include "noncopyable.h"

class Channel;
class EventLoop;

// muoduo库中多路事件分发器的核心I/O复用模块
class Poller : noncopyable
{
public:
    using ChannelList = std::vector<Channel*>;

    Poller(EventLoop* loop);
    virtual ~Poller() = default;

    // 给所有的I/O复用保留统一的接口
    virtual Timestamp poll(int timeoutMs, ChannelList* artiveChannels) = 0;
    virtual void updateChannel(Channel* channel) = 0;
    virtual void removeChannel(Channel* channel) = 0;

    // 判断参数channel是否在当前的Poller中
    virtual bool hasChannel(Channel* channel) const;

    // EventLoop可以通过该接口获取默认的I/O复用的具体实现
    static Poller* newDefaultPoller(EventLoop* loop);

protected:
    // key: sockfd, val: sockfd所属的Channel
    using ChannelMap = std::unordered_map<int, Channel*>;
    ChannelMap channels_;

private:
    EventLoop* ownerLoop_;  // 定义Poller所属的事件循环EventLoop
};