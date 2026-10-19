#pragma once
#include "rtos_sync.hpp"
namespace rtos {
struct MutexInfo {const Thread* owner{};size_t waiters{};bool owner_dead{};};
/** @brief Nonrecursive thread-owned mutex with bounded priority inheritance.
 * NoWait returns busy; finite waits reject dependency cycles. Owner exit faults
 * the mutex and wakes waiters with owner_dead rather than silently transferring
 * potentially unsafe device state. Never use from an ISR or as an SMP lock.
 */
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
    /// Acquire ownership; priority is donated through a bounded acyclic wait chain.
    Result lock(Tick timeout=NoWait);
    /// Only the owner may release; ownership passes to a waiter before it becomes ready.
    Result unlock();
    // Caller asserts that protected state/hardware is safe. No automatic hardware reset.
    Result recover_after_quiescence();
    MutexInfo snapshot()const;
    /// Kernel hooks: caller already serializes scheduler metadata; not application APIs.
    static void priority_changed(Thread&);
    static void owner_exit(Thread&);
};
/// Scoped ownership only on successful lock; normal error returns release the mutex.
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
/** @brief Serialize an entire command/data/status transaction for one device.
 * The acquisition timeout does not bound callback execution or a stalled AXI
 * bus access. The driver must supply operation deadlines and hardware recovery.
 * Keep this object permanent and shared by every client of that same device.
 */
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
