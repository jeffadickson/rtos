#pragma once
#include "rtos.hpp"
#if !RTOS_SYNC
#error "Enable RTOS_SYNC to use synchronization services"
#endif
#include "rtos/detail/wait_queue.hpp"
namespace rtos {
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
enum class EventClear : uint8_t { no, yes };
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
    Result wait_any(uint32_t mask,uint32_t& observed,Tick timeout=NoWait,EventClear clear=EventClear::no){return wait(mask,observed,timeout,false,clear==EventClear::yes);}
    Result wait_all(uint32_t mask,uint32_t& observed,Tick timeout=NoWait,EventClear clear=EventClear::no){return wait(mask,observed,timeout,true,clear==EventClear::yes);}
    Result set(uint32_t bits);
    Result set_from_isr(uint32_t bits);
    Result clear(uint32_t bits);
    EventInfo snapshot()const;
};
}
