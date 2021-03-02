#include "Buffer.h"

#include <errno.h>
#include <sys/uio.h>

/**
 * 从fd上读取数据，Poller工作在LT模式下
 * Buffer缓冲区是有大小的，但是从fd上读数据的时候却不知道tcp流式数据的最终大小
 */
ssize_t Buffer::readFd(int fd, int* savedErrno)
{
    char extrabuf[65536] = {0};

    struct iovec vec[2];
    const size_t writeable = writeableBytes();  // buffer_底层缓冲区剩余可写空间大小
    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len = writeable;

    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof extrabuf;

    const int iovcnt = (writeable < sizeof extrabuf) ? 2 : 1;
    const size_t n = ::readv(fd, vec, iovcnt);
    if (n < 0)
    {
        *savedErrno = errno;
    }
    else if (n <= writeable)
    {
        writerIndex_ += n;
    }
    else
    {
        // buffer_底层缓冲区可写空间都已经写满了，readv将没读完的数据写入到extrabuf中
        writerIndex_ = buffer_.size();
        append(extrabuf, n - writeable);
    }

    return n;
}

// 通过fd发送数据
ssize_t Buffer::writeFd(int fd, int* saveErrno)
{
    ssize_t n = ::write(fd, peek(), readableBytes());
    if (n < 0)
    {
        *saveErrno = errno;
    }
    return n;
}