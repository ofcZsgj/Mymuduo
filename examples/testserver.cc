#include <mymuduo/Logger.h>
#include <mymuduo/TcpServer.h>

#include <functional>
#include <string>

class EchoServer
{
public:
    EchoServer(EventLoop *loop,
               const InetAddress &addr,
               const std::string &name)
        : server_(loop, addr, name),
          loop_(loop)
    {
        // 注册回调函数
        server_.setConnectionCallback(
            std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
        server_.setMessageCallback(
            std::bind(&EchoServer::onMessage,
                      this,
                      std::placeholders::_1,
                      std::placeholders::_2,
                      std::placeholders::_3));
        // 设置合适的loop线程数量 loopthread n+1
        server_.setThreadNumber(3);
    }

    void start()
    {
        server_.start();
    }

private:
    // 连接建立或断开回调
    void onConnection(const TcpConnectionPtr &conn)
    {
        if (conn->connected())
        {
            LOG_INFO("conn UP : %s", conn->peerAddress().toIpPort().c_str());
        }
        else
        {
            LOG_INFO("conn DOWN : %s", conn->peerAddress().toIpPort().c_str());
        }
    }

    // 可读写事件回调
    void onMessage(const TcpConnectionPtr &conn,
                   Buffer *buffer,
                   Timestamp time)
    {
        std::string msg = buffer->retrieveAllAsString();
        conn->send(msg);
        conn->shutdown();
    }

    EventLoop *loop_;
    TcpServer server_;
};

int main()
{
    EventLoop loop;
    InetAddress addr(8000);
    // Acceptor non-blocking listenfd create bind
    EchoServer server(&loop, addr, "EchoServer-01");
    // listen loopthread listenfd -> acceptChannel -> mainloop -> subloop
    server.start();
    loop.loop();  // 启动mainLoop的底层Poller epoll_wait
    return 0;
}