#pragma once
#include "rtos.hpp"
#if !RTOS_SYNC
#error "Enable RTOS_SYNC to use synchronization services"
#endif
#include "rtos/detail/wait_queue.hpp"
namespace rtos {
struct SemaphoreInfo { uint32_t count{},maximum{};size_t waiters{}; };
/** @brief Boot-static counted tokens, with priority/FIFO waiter selection.
 * Invalid constructor bounds remain invalid; release never exceeds maximum.
 * Tokens have no owner or priority inheritance: use Mutex for a shared device.
 */
class Semaphore {
    detail::WaitQueue waiters_;
    uint32_t count_,maximum_;
    bool valid_;
    Result release_locked(detail::Critical&);
public:
    constexpr Semaphore(uint32_t initial,uint32_t maximum):count_(initial),maximum_(maximum),valid_(maximum>0&&initial<=maximum){}
    Semaphore(const Semaphore&)=delete;
    Semaphore& operator=(const Semaphore&)=delete;
    /// Consume a token or wait at most timeout ticks; NoWait returns empty.
    Result acquire(Tick timeout=NoWait);
    /// Hand a token directly to a waiter, otherwise increment; overflow loses no token.
    Result release();
    /// Nonblocking ISR notification; requires a verified active port ISR context.
    Result release_from_isr();
    SemaphoreInfo snapshot()const;
};
enum class EventClear : uint8_t { no, yes };
struct EventInfo { uint32_t bits{};size_t waiters{}; };
/** @brief Sticky readiness bits; repeated sets of the same bit are coalesced.
 * Waits are priority ordered, FIFO for ties. Non-consuming waiters observe the
 * set operation's broadcast snapshot; consuming waiters compete for stored bits.
 * Events do not carry data: pair them with a protected buffer or copied Queue.
 */
class EventFlags {
    detail::WaitQueue waiters_;
    uint32_t bits_{};
    Result set_locked(uint32_t,detail::Critical&);
public:
    constexpr EventFlags()=default;
    EventFlags(const EventFlags&)=delete;
    EventFlags& operator=(const EventFlags&)=delete;
    /// Legacy combined form; zero mask/oversized wait is invalid, failures preserve observed.
    Result wait(uint32_t mask,uint32_t& observed,Tick timeout=NoWait,bool all=false,bool clear=false);
    /// Observe any requested bit; EventClear::yes consumes only the observed bits.
    Result wait_any(uint32_t mask,uint32_t& observed,Tick timeout=NoWait,EventClear clear=EventClear::no){return wait(mask,observed,timeout,false,clear==EventClear::yes);}
    /// Observe every requested bit together; partial availability does not consume bits.
    Result wait_all(uint32_t mask,uint32_t& observed,Tick timeout=NoWait,EventClear clear=EventClear::no){return wait(mask,observed,timeout,true,clear==EventClear::yes);}
    /// Set nonzero bits and complete eligible waiters before requesting rescheduling.
    Result set(uint32_t bits);
    Result set_from_isr(uint32_t bits);
    /// Explicitly clear nonzero bits from thread context; does not wake waiters.
    Result clear(uint32_t bits);
    EventInfo snapshot()const;
};
}
