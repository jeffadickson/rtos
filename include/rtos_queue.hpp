#pragma once
#include "rtos_sync.hpp"
namespace rtos {
struct QueueInfo {size_t depth{},capacity{},high_water{},send_waiters{},receive_waiters{};uint32_t full{},empty{},timeouts{};};
namespace detail {inline void increment(uint32_t& value){if(value!=UINT32_MAX)++value;}}
// All storage is permanent. Copies are bounded by the configured payload cap.
template<class T,size_t Capacity> class Queue {
    static_assert(Capacity>0,"Queue capacity must be positive");
    static_assert(__is_trivially_copyable(T)&&__is_trivially_constructible(T),"Messages must be trivial value types");
    static_assert(sizeof(T)<=RTOS_IPC_MAX_MESSAGE_BYTES,"Message exceeds interrupt/critical-section copy bound");
    T slots_[Capacity]{};
    size_t head_{},used_{},high_water_{};
    uint32_t full_{},empty_{},timeouts_{};
    detail::WaitQueue senders_,receivers_;
    Result send_locked(const T& value,detail::Critical& lock){
        if(auto* t=receivers_.best()){
            *static_cast<T*>(t->wait.data)=value;
            receivers_.complete(*t,Result::ok);lock.changed();return Result::ok;
        }
        if(used_==Capacity){detail::increment(full_);return Result::full;}
        slots_[(head_+used_)%Capacity]=value;
        ++used_;if(used_>high_water_)high_water_=used_;
        return Result::ok;
    }
    Result receive_locked(T& output,detail::Critical& lock){
        if(!used_){detail::increment(empty_);return Result::empty;}
        output=slots_[head_];head_=(head_+1)%Capacity;--used_;
        if(auto* t=senders_.best()){
            slots_[(head_+used_)%Capacity]=*static_cast<const T*>(t->wait.data);++used_;
            senders_.complete(*t,Result::ok);lock.changed();
        }
        return Result::ok;
    }
public:
    constexpr Queue()=default;
    Queue(const Queue&)=delete;
    Queue& operator=(const Queue&)=delete;
    Result send(const T& value,Tick timeout=NoWait){
        detail::Critical lock;
        if(!detail::calling_thread())return Result::context;
        if(timeout>MaxWait)return Result::invalid;
        auto result=send_locked(value,lock);
        if(result!=Result::full||!timeout)return result;
        T staged=value; // The sleeping sender owns this bounded copy on its boot-provided stack.
        result=senders_.wait(timeout,&staged);
        if(result==Result::timeout)detail::increment(timeouts_);
        return result;
    }
    Result receive(T& output,Tick timeout=NoWait){
        detail::Critical lock;
        if(!detail::calling_thread())return Result::context;
        if(timeout>MaxWait)return Result::invalid;
        auto result=receive_locked(output,lock);
        if(result!=Result::empty||!timeout)return result;
        result=receivers_.wait(timeout,&output);
        if(result==Result::timeout)detail::increment(timeouts_);
        return result;
    }
    Result try_send_from_isr(const T& value){
        detail::Critical lock;
        if(!detail::interrupt_context()||!detail::synchronization_running())return Result::context;
        return send_locked(value,lock);
    }
    Result try_receive_from_isr(T& output){
        detail::Critical lock;
        if(!detail::interrupt_context()||!detail::synchronization_running())return Result::context;
        return receive_locked(output,lock);
    }
    QueueInfo snapshot()const{detail::Critical lock;return {used_,Capacity,high_water_,senders_.size(),receivers_.size(),full_,empty_,timeouts_};}
};
}
