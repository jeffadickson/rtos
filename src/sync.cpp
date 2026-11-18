#include "rtos_sync.hpp"
#include "rtos_monitor.hpp"
namespace rtos::detail {
Thread* WaitQueue::best()const {
    // Enrollment order is stable; strict '>' preserves FIFO among equal priorities.
    Thread* chosen=nullptr;
    for(auto* t=first_;t;t=t->wait.next)
        if(!chosen||t->effective_priority>chosen->effective_priority)chosen=t;
    return chosen;
}
Result WaitQueue::wait(Tick timeout,void* data,uint32_t mask,uint8_t flags){
    // Caller holds Critical across enrollment and ECALL: no wakeup can be lost
    // between testing availability, publishing this node and becoming blocked.
    auto* t=calling_thread();
    if(!t||t->state!=State::ready||t->wait.queue)return Result::context;
    if(timeout==0||timeout>MaxWait)return Result::invalid;
    t->wait.queue=this;t->wait.next=nullptr;t->wait.data=data;t->wait.mask=mask;
    t->wait.flags=flags;t->wait.observed=0;t->wait.result=Result::busy;
    t->wake=synchronization_now()+timeout;t->state=State::blocked;
    NEXRT_TRACE(blocked,t,nullptr,timeout);
    if(last_)last_->wait.next=t;else first_=t;
    last_=t;++count_;
    if(on_change)on_change(observer);
    park_thread();
    return t->wait.result;
}
void WaitQueue::complete(Thread& t,Result result){
    // Remove every enrollment/payload link before waking. Queue senders may point
    // into a suspended stack; no later timeout or handoff may reuse that pointer.
    if(t.wait.queue!=this)return;
    Thread* previous=nullptr;auto* p=first_;
    while(p&&p!=&t){previous=p;p=p->wait.next;}
    if(!p)return;
    if(previous)previous->wait.next=t.wait.next;else first_=t.wait.next;
    if(last_==&t)last_=previous;
    t.waiting_mutex=nullptr;
    --count_;t.wait.queue=nullptr;t.wait.next=nullptr;t.wait.data=nullptr;
    t.wait.result=result;t.state=State::ready;
    NEXRT_TRACE(ready,&t,nullptr,uint32_t(result));
    if(on_change)on_change(observer);
}
void expire_wait(Thread& t,Tick now){
    // Half-range comparisons are valid because every accepted delay is <= MaxWait.
    if(t.state==State::blocked&&t.wait.queue&&Tick(now-t.wake)<0x80000000u)
        t.wait.queue->complete(t,Result::timeout);
}
void abandon_wait(Thread& t){if(t.wait.queue)t.wait.queue->complete(t,Result::owner_dead);}
}
namespace rtos {
Result Semaphore::acquire(Tick timeout){
    detail::Critical lock;
    if(!detail::calling_thread())return Result::context;
    if(!valid_||timeout>MaxWait)return Result::invalid;
    if(count_){--count_;return Result::ok;}
    return timeout?waiters_.wait(timeout):Result::empty;
}
Result Semaphore::release_locked(detail::Critical& lock){
    if(!valid_)return Result::invalid;
    if(auto* t=waiters_.best()){waiters_.complete(*t,Result::ok);lock.changed();return Result::ok;}
    if(count_==maximum_)return Result::overflow;
    ++count_;return Result::ok;
}
Result Semaphore::release(){detail::Critical lock;if(!detail::calling_thread())return Result::context;return release_locked(lock);}
Result Semaphore::release_from_isr(){detail::Critical lock;if(!detail::interrupt_context()||!detail::synchronization_running())return Result::context;return release_locked(lock);}
SemaphoreInfo Semaphore::snapshot()const{detail::Critical lock;return {count_,maximum_,waiters_.size()};}
Result EventFlags::wait(uint32_t mask,uint32_t& observed,Tick timeout,bool all,bool clear_on_success){
    detail::Critical lock;
    if(!detail::calling_thread())return Result::context;
    if(!mask||timeout>MaxWait)return Result::invalid;
    uint32_t found=bits_&mask;
    if(all?found==mask:found!=0){observed=found;if(clear_on_success)bits_&=~found;return Result::ok;}
    if(!timeout)return Result::empty;
    auto* t=detail::calling_thread();
    auto result=waiters_.wait(timeout,nullptr,mask,uint8_t((all?detail::wait_all_bits:0)|(clear_on_success?detail::consume_bits:0)));
    if(result==Result::ok)observed=t->wait.observed;
    return result;
}
Result EventFlags::set_locked(uint32_t bits,detail::Critical& lock){
    if(!bits)return Result::invalid;
    // Non-consumers must see the original set even if an earlier consumer clears it.
    bits_|=bits;const auto broadcast=bits_;
    // Select only eligible waiters, by effective priority then enrollment order.
    for(;;){Thread* chosen=nullptr;
        for(auto* t=waiters_.first();t;t=t->wait.next){auto found=((t->wait.flags&detail::consume_bits)?bits_:broadcast)&t->wait.mask;bool eligible=(t->wait.flags&detail::wait_all_bits)?found==t->wait.mask:found!=0;
            if(eligible&&(!chosen||t->effective_priority>chosen->effective_priority))chosen=t;}
        if(!chosen)break;
        auto found=((chosen->wait.flags&detail::consume_bits)?bits_:broadcast)&chosen->wait.mask;chosen->wait.observed=found;
        if(chosen->wait.flags&detail::consume_bits)bits_&=~found;
        waiters_.complete(*chosen,Result::ok);lock.changed();
    }
    return Result::ok;
}
Result EventFlags::set(uint32_t bits){detail::Critical lock;if(!detail::calling_thread())return Result::context;return set_locked(bits,lock);}
Result EventFlags::set_from_isr(uint32_t bits){detail::Critical lock;if(!detail::interrupt_context()||!detail::synchronization_running())return Result::context;return set_locked(bits,lock);}
Result EventFlags::clear(uint32_t bits){detail::Critical lock;if(!detail::calling_thread())return Result::context;if(!bits)return Result::invalid;bits_&=~bits;return Result::ok;}
EventInfo EventFlags::snapshot()const{detail::Critical lock;return {bits_,waiters_.size()};}
}
