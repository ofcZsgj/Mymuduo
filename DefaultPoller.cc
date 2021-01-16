#include <stdlib.h>

#include "EPollPoller.h"
#include "Poller.h"

// 为了避免在Poller.cc引入EPollPoller/PollPoller的头文件即避免基类依赖派生类，将此方法放在Poller.cc实现
Poller* Poller::newDefaultPoller(EventLoop* loop)
{
    if (::getenv("MUDUO_USE_POLL"))  // 获取系统环境变量来判断使用poll还是epoll
    {
        return nullptr;  // TODO: 生成poll的实例
    }
    else
    {
        return new EPollPoller(loop);  // 生成epoll的实例
    }
}