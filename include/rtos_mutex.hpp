#pragma once
#include "rtos_sync.hpp"
namespace rtos {
struct MutexInfo {const Thread* owner{};size_t waiters{};bool owner_dead{};};
class Mutex {
    detail::WaitQueue waiters_;
    Thread* owner_{};
    Mutex* next_owned_{};
    bool faulted_{};
    void attach(Thread&);
    void detach();
    static void changed(void*);
    void abandon(Thread&);
public:
    constexpr Mutex(){waiters_.observer=this;waiters_.on_change=&changed;}
    Mutex(const Mutex&)=delete;
    Mutex& operator=(const Mutex&)=delete;
    Result lock(Tick timeout=NoWait);
    Result unlock();
    // Caller asserts that protected state/hardware is safe. No automatic hardware reset.
    Result recover_after_quiescence();
    MutexInfo snapshot()const;
    static void priority_changed(Thread&);
    static void owner_exit(Thread&);
};
class LockGuard {
    Mutex& mutex_;
    Result result_;
public:
    explicit LockGuard(Mutex& mutex,Tick timeout=NoWait):mutex_(mutex),result_(mutex.lock(timeout)){}
    ~LockGuard(){if(result_==Result::ok)(void)mutex_.unlock();}
    LockGuard(const LockGuard&)=delete;
    LockGuard& operator=(const LockGuard&)=delete;
    Result result()const{return result_;}
    bool owns_lock()const{return result_==Result::ok;}
};
// Wrap the entire logical command/data/status transaction, including completion waits.
class DeviceTransactions {
    Mutex mutex_;
public:
    constexpr DeviceTransactions()=default;
    DeviceTransactions(const DeviceTransactions&)=delete;
    template<class Operation> Result transact(Tick timeout,Operation&& operation){
        LockGuard guard(mutex_,timeout);
        if(!guard.owns_lock())return guard.result();
        return operation(); // Callback must return Result and obey bounded-operation contracts.
    }
    MutexInfo snapshot()const{return mutex_.snapshot();}
    Result recover_after_quiescence(){return mutex_.recover_after_quiescence();}
};
}
