#pragma once

/**
 * noncopyablel 被继承后，派生类对象能够正常的构造和析构，但是无法进行拷贝和赋值
 */
class noncopyable
{
public:
    noncopyable(const noncopyable &) = delete;
    noncopyable &operator=(const noncopyable &) = delete;

protected:
    noncopyable() = default;
    ~noncopyable() = default;
};
