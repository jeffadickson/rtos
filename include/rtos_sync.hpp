#pragma once
#include "rtos.hpp"
#if !RTOS_SYNC
#error "Enable RTOS_SYNC to use synchronization services"
#endif
namespace rtos {
using Result = detail::WaitResult;
constexpr Tick NoWait = 0;
// Relative waits deliberately exclude infinity: every blocked operation has a finite deadline.
constexpr Tick MaxWait = 0x7fffffffu;
namespace detail {
using CriticalState = uintptr_t;
CriticalState critical_enter();
void critical_leave(CriticalState);
Thread* calling_thread();
bool interrupt_context();
bool synchronization_running();
Tick synchronization_now();
void park_thread();
void schedule_ready();
class Critical {
    CriticalState saved_;
    bool schedule_{};
public:
    Critical():saved_(critical_enter()){}
    ~Critical(){critical_leave(saved_);if(schedule_)schedule_ready();}
    Critical(const Critical&)=delete;
    Critical& operator=(const Critical&)=delete;
    void changed(){schedule_=true;}
};
class WaitQueue {
    Thread* first_{};
    Thread* last_{};
    size_t count_{};
public:
    void* observer{};
    void (*on_change)(void*){};
    constexpr WaitQueue()=default;
    WaitQueue(const WaitQueue&)=delete;
    WaitQueue& operator=(const WaitQueue&)=delete;
    Thread* best() const;
    Thread* first() const{return first_;}
    size_t size()const{return count_;}
    Result wait(Tick timeout,void* data=nullptr,uint32_t mask=0,uint8_t flags=0);
    void complete(Thread&,Result);
};
void expire_wait(Thread&,Tick);
void abandon_wait(Thread&);
}
struct SemaphoreInfo { uint32_t count{},maximum{};size_t waiters{}; };
class Semaphore {
    detail::WaitQueue waiters_;
    uint32_t count_,maximum_;
    bool valid_;
    Result release_locked(detail::Critical&);
public:
    constexpr Semaphore(uint32_t initial,uint32_t maximum):count_(initial),maximum_(maximum),valid_(maximum>0&&initial<=maximum){}
    Semaphore(const Semaphore&)=delete;
    Semaphore& operator=(const Semaphore&)=delete;
    Result acquire(Tick timeout=NoWait);
    Result release();
    Result release_from_isr();
    SemaphoreInfo snapshot()const;
};
struct EventInfo { uint32_t bits{};size_t waiters{}; };
class EventFlags {
    detail::WaitQueue waiters_;
    uint32_t bits_{};
    Result set_locked(uint32_t,detail::Critical&);
public:
    constexpr EventFlags()=default;
    EventFlags(const EventFlags&)=delete;
    EventFlags& operator=(const EventFlags&)=delete;
    Result wait(uint32_t mask,uint32_t& observed,Tick timeout=NoWait,bool all=false,bool clear=false);
    Result set(uint32_t bits);
    Result set_from_isr(uint32_t bits);
    Result clear(uint32_t bits);
    EventInfo snapshot()const;
};
}
