#include "CurrentThread.h"

namespace CurrentThread
{
    __thread int t_cachedTid = 0;

    void cachTid()
    {
        if (t_cachedTid == 0)
        {
            // 通过Linux系统调用获取当前的线程的tid
            t_cachedTid = static_cast<pid_t>(::syscall(SYS_getpid));
        }
    }

}  // namespace CurrentThread