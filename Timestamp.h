#pragma once

#include <iostream>
#include <string>

class Timestamp
{
public:
    Timestamp() : microSecondsSinceEpochArg_(0)
    {
    }
    explicit Timestamp(int64_t microSecondsSinceEpochArg);
    static Timestamp now();
    std::string toString() const;

private:
    int64_t microSecondsSinceEpochArg_;
};
