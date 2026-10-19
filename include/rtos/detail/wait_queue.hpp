#pragma once
#include "../../rtos.hpp"
// Kernel implementation detail. Applications should include rtos_sync.hpp.
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
    // A completion requests scheduling only after restoring the saved IRQ state.
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
    // One embedded node per registered thread. No allocation, copied node or
    // cancellation race; all list accesses require the same single-hart lock.
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
}
