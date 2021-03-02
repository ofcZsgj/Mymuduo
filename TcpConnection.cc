#include "TcpConnection.h"

#include <errno.h>

#include <functional>

#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"
#include "Socket.h"

static EventLoop *CheckLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d TcpConnection loop is nullptr\n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpConnection::TcpConnection(EventLoop *loop,
                             const std::string &nameArg,
                             int sockfd,
                             const InetAddress localAddr,
                             const InetAddress peerAddr)
    : loop_(CheckLoopNotNull(loop)),
      name_(nameArg),
      state_(kConnecting),
      readinig_(true),
      socket_(new Socket(sockfd)),
      channel_(new Channel(loop, sockfd)),
      localAddr_(localAddr),
      peerAddr_(peerAddr),
      highWaterMark_(64 * 1024 * 1024)  // 64M
{
    // 给channel设置相应的回调函数，poller给channel通知感兴趣的事件发生后，channel会回调相应的操作函数
    channel_->setReadCallback(std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
    channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
    channel_->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
    channel_->setErrorCallback(std::bind(&TcpConnection::handleError, this));

    LOG_INFO("TcpConnection::ctor[%s] at fd=%d\n", name_.c_str(), sockfd);
    socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection()
{
    LOG_INFO("TcpConnectin::dtor[%s] at fd=%d state=%d\n", name_.c_str(), channel_->fd(), (int)state_);
}

void TcpConnection::handleRead(Timestamp receiveTime)
{
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if (n > 0)
    {
        // 已经建立连接的用户有可读事件发生了，调用用户传入的回调操作onMessage
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
    else if (n == 0)
    {
        handleClose();
    }
    else
    {
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead");
        handleError();
    }
}

void TcpConnection::handleWrite()
{
    if (channel_->isWriteEvent())
    {
        int savedErrno = 0;
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
        if (n > 0)
        {
            outputBuffer_.retrieve(n);
            if (outputBuffer_.readableBytes() == 0)
            {
                channel_->disableWriting();
                if (writeCompleteCallback_)
                {
                    // 唤醒loop_对应的thread线程执行相应的回调
                    loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
                }
                // shutdown时还有数据未发送会设置kDisconnection等待发送完成后调用shutdownInLoop关闭写端
                if (kDisconnecting == state_)
                {
                    shutdownInLoop();
                }
            }
        }
        else
        {
            LOG_ERROR("TcpConnection::handleWrite");
        }
    }
    else
    {
        LOG_ERROR("TcpConnection fd=%d is down, no more writing\n", channel_->fd());
    }
}

// shutdownInLoop()关闭写端后触发EPOLLHUP调用closeCallback_即TcpConnection::handleClose()
// poller -> channel::closeCallback_
void TcpConnection::handleClose()
{
    LOG_INFO("fd=%d, state=%d\n", channel_->fd(), (int)state_);
    setState(kDisconnected);

    TcpConnectionPtr connPtr(shared_from_this());  // 获取当前对象
    connectionCallback_(connPtr);                  // 执行连接关闭的回调
    // 关闭连接的回调 执行的是TcpServer::removeConnection回调方法
    closeCallback_(connPtr);
}

void TcpConnection::handleError()
{
    int optval;
    socklen_t optlen = static_cast<socklen_t>(sizeof optval);
    int err = 0;
    if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        err = errno;
    }
    else
    {
        err = optval;
    }
    LOG_ERROR("TcpConnection::handleErrno name:%s - SO_ERRNO:%d\n", name_.c_str(), err);
}

void TcpConnection::send(const std::string &buf)
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread())  // 不排除有记录下TcpCon连接在其他线程进行发送的情况
        {
            sendInLoop(buf.c_str(), buf.size());
        }
        else
        {
            loop_->runInLoop(std::bind(
                &TcpConnection::sendInLoop,
                this,
                buf.c_str(),
                buf.size()));
        }
    }
}

// 关闭连接
void TcpConnection::shutdown()
{
    if (state_ == kConnected)
    {
        setState(kDisconnecting);
        loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
    }
}

// 关闭连接的核心
void TcpConnection::shutdownInLoop()
{
    // 确保outputBuffer中的数据已经全部发送完成，如果还有正在写的，会等待写完并在sendInLoop再次调用该方法
    if (!channel_->isWriteEvent())
    {
        // 关闭写端，会触发EPOLLHUP事件即调用channel的closeCallback
        socket_->shutdownWrite();
    }
}

// 发送数据 应用写得快 内核发送数据慢，需要把待发送数据写入缓冲区而且设置了水位线回调
void TcpConnection::sendInLoop(const void *data, size_t len)
{
    ssize_t nwrote = 0;
    ssize_t remaining = len;
    bool faultError = false;

    // 之前调用过该conn的shutdown
    if (state_ = kDisconnected)
    {
        LOG_ERROR("disconnected, give up writing");
        return;
    }

    // channel第一次开始写数据，而且缓冲区没有待发送数据
    if (!channel_->isWriteEvent() && outputBuffer_.readableBytes())
    {
        nwrote = ::write(channel_->fd(), data, len);
        remaining = len - nwrote;
        if (nwrote >= 0)
        {
            // 若此时已经发送完数据，就不需要再给channel注册EpollOut事件了
            if (remaining == 0 && writeCompleteCallback_)
            {
                loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
            }
        }
        else
        {
            nwrote = 0;
            if (errno != EWOULDBLOCK)  // 如果不是 由于非阻塞正常返回
            {
                LOG_ERROR("TcpConnection::sendInLoop");
                // SIGPIPE RESET 接收到对端socket重重情况
                if (errno == EPIPE || errno == ECONNRESET)
                {
                    faultError = true;
                }
            }
        }
    }

    // 当前这一次write并没有把数据全部发送出去，需要保存到缓冲区当中，并给channel注册epollout事件
    // poller发现tcp的发送缓冲区有空间，会不断通知相应的sock-channel，执行writeCallback_回调方法
    // 即通过调用TcpConnection::handleWrite方法将缓冲区中的数据全部发送完成
    if (!faultError && remaining > 0)
    {
        // 目前发送缓冲区剩余的待发送数据的长度
        size_t oldLen = outputBuffer_.readableBytes();
        if (oldLen + remaining > highWaterMark_ &&
            oldLen < highWaterMark_ &&
            highWaterMarkCallback_)
        {
            loop_->queueInLoop(std::bind(
                highWaterMarkCallback_,
                shared_from_this(),
                oldLen + remaining));
        }
        outputBuffer_.append((char *)data + nwrote, remaining);  // 待缓冲的长度
        if (channel_->isWriteEvent())
        {
            // 注册channel的写事件否则poller不会给channel通知epollout事件执行回调
            channel_->enableWriting();
        }
    }
}

// 连接建立
void TcpConnection::connectEstablished()
{
    setState(kConnected);
    // channel中设置弱智能指针的目的：由于TcpConnection是对外提供的，用户可能对其做任何的操作
    // channel的回调方法是TcpConnection绑定的成员方法，为避免发生未知的错误
    // 因此需要tie()使得channel在调用TcpConnection给channel设置的回调方法时，TcpConnection对象存在
    channel_->tie(shared_from_this());
    channel_->enableReading();  // 向poller注册epollin事件

    //新连接建立，执行回调
    connectionCallback_(shared_from_this());
}

// 连接销毁
void TcpConnection::connectDestroyed()
{
    if (state_ == kConnected)
    {
        setState(kDisconnected);
        channel_->disableAll();  // 把channel所有感兴趣的事件都从poller中del

        connectionCallback_(shared_from_this());
    }
    channel_->remove();  // 把channel从poller中删除
}