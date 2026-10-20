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
    /// Read one RAM word while preserving the caller's prior interrupt mask.
    uint32_t load()const{detail::Critical lock;return value_;}
    /// Replace one word; serialization is local to the implemented single CPU.
    void store(uint32_t desired){detail::Critical lock;value_=desired;}
    /// Replace the value and return its prior contents in one critical section.
    uint32_t exchange(uint32_t desired){detail::Critical lock;auto prior=value_;value_=desired;return prior;}
    /// On mismatch update expected with the observed value; never spuriously fail.
    bool compare_exchange(uint32_t& expected,uint32_t desired){
        detail::Critical lock;
        if(value_==expected){value_=desired;return true;}
        expected=value_;return false;
    }
    /// Fetch operations return the previous value; unsigned addition wraps modulo 2^32.
    uint32_t fetch_add(uint32_t value){detail::Critical lock;auto prior=value_;value_+=value;return prior;}
    /// Set selected bits and return the prior word; never operate on device MMIO.
    uint32_t fetch_or(uint32_t value){detail::Critical lock;auto prior=value_;value_|=value;return prior;}
    /// Keep selected bits and return the prior word; no cross-CPU ordering contract.
    uint32_t fetch_and(uint32_t value){detail::Critical lock;auto prior=value_;value_&=value;return prior;}
};
}
