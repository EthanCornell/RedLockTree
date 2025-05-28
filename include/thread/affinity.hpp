#ifndef UTIL_AFFINITY_HPP
#define UTIL_AFFINITY_HPP

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace util
{

inline bool use_core(int id)
{
#ifdef _WIND32
    HANDLE    thread = GetCurrentThread();
    DWORD_PTR mask   = (1ULL << id)
    return SetThreadAffinity(thread, mask) != 0;
#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(id, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
#elif defined(__APPLE__)
    thread_affinity_policy_data_t policy = {id};
    return thread_policy_set(pthread_mach_thread_np(pthread_self()),
			   THREAD_AFFINITY_POLICY, (thread_policy_t)&policy, 1) == KERN_SUCCESS;
#else
    return false;
#endif
}

}  // namespace util

#endif  // UTIL_AFFINITY_HPP
