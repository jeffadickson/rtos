#pragma once
#include "rtos_sync.hpp"
namespace rtos {
// Single-hart interrupt-serialized RAM word. Not lock-free, DMA-coherent or SMP-safe.
// All accesses must go through this object. Do not place it over MMIO.
class AtomicU32 {
    uint32_t value_;
public:
    constexpr explicit AtomicU32(uint32_t initial=0):value_(initial){}
    AtomicU32(const AtomicU32&)=delete;
    AtomicU32& operator=(const AtomicU32&)=delete;
    uint32_t load()const{detail::Critical lock;return value_;}
    void store(uint32_t desired){detail::Critical lock;value_=desired;}
    uint32_t exchange(uint32_t desired){detail::Critical lock;auto prior=value_;value_=desired;return prior;}
    bool compare_exchange(uint32_t& expected,uint32_t desired){
        detail::Critical lock;
        if(value_==expected){value_=desired;return true;}
        expected=value_;return false;
    }
    uint32_t fetch_add(uint32_t value){detail::Critical lock;auto prior=value_;value_+=value;return prior;}
    uint32_t fetch_or(uint32_t value){detail::Critical lock;auto prior=value_;value_|=value;return prior;}
    uint32_t fetch_and(uint32_t value){detail::Critical lock;auto prior=value_;value_&=value;return prior;}
};
}
