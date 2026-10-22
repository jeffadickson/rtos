#pragma once
#include "rtos_sync.hpp"
#if RTOS_ZAAMO
#include "rtos_hal_atomic.hpp"
#endif
namespace rtos {
/** @brief Fixed-size single-CPU RAM atomic word, usable from thread/ISR contexts.
 * Initialize before sharing and keep storage alive for every access; no
 * registration or heap is required. All accesses must use this object, with no
 * overlapping raw aliases.
 * The default implementation saves/masks/restores local scheduling interrupts.
 * RTOS_ZAAMO substitutes hardware RMWs for store/exchange/add/OR/AND only; load
 * and compare_exchange retain interrupt masking. No operation waits for a thread
 * mutex, but hardware accesses can stall if the platform memory system stalls.
 * This is not std::atomic, an MMIO accessor, an SMP lock or a DMA-coherence API.
 * Other harts/devices must not write this storage concurrently: local masking
 * cannot protect compare_exchange from those writers even with Zaamo enabled.
 */
class AtomicU32 {
    alignas(4) uint32_t value_;
public:
    /// Constant initialization reserves exactly one aligned word, initially initial.
    constexpr explicit AtomicU32(uint32_t initial=0):value_(initial){}
    AtomicU32(const AtomicU32&)=delete;
    AtomicU32& operator=(const AtomicU32&)=delete;
    /// Read one RAM word while preserving the caller's prior interrupt mask.
    uint32_t load()const{detail::Critical lock;return value_;}
    /// Replace the word. Hardware mode discards AMOSWAP's old value; default mode masks IRQs.
    void store(uint32_t desired){
#if RTOS_ZAAMO
        (void)hal::atomic::exchange(&value_,desired);
#else
        detail::Critical lock;value_=desired;
#endif
    }
    /// Replace the word and return its old value: one AMO or one local critical section.
    uint32_t exchange(uint32_t desired){
#if RTOS_ZAAMO
        return hal::atomic::exchange(&value_,desired);
#else
        detail::Critical lock;auto prior=value_;value_=desired;return prior;
#endif
    }
    /** @brief Compare against expected and conditionally replace the word.
     * Success writes desired and leaves expected unchanged. Failure leaves the
     * word unchanged and replaces expected with the observed word; no spurious
     * failures occur. This ALWAYS uses local interrupt masking, including in
     * Zaamo mode, because Zaamo supplies neither CAS nor LR/SC. expected must be
     * separate caller-owned RAM, not an alias of this object's stored word.
     */
    bool compare_exchange(uint32_t& expected,uint32_t desired){
        detail::Critical lock;
        if(value_==expected){value_=desired;return true;}
        expected=value_;return false;
    }
    /// Add modulo 2^32 and return the old value, using one AMO or a local critical section.
    uint32_t fetch_add(uint32_t value){
#if RTOS_ZAAMO
        return hal::atomic::fetch_add(&value_,value);
#else
        detail::Critical lock;auto prior=value_;value_+=value;return prior;
#endif
    }
    /// Set selected bits and return the prior word; never operate on device MMIO.
    uint32_t fetch_or(uint32_t value){
#if RTOS_ZAAMO
        return hal::atomic::fetch_or(&value_,value);
#else
        detail::Critical lock;auto prior=value_;value_|=value;return prior;
#endif
    }
    /// Keep selected bits and return the prior word; no cross-CPU ordering contract.
    uint32_t fetch_and(uint32_t value){
#if RTOS_ZAAMO
        return hal::atomic::fetch_and(&value_,value);
#else
        detail::Critical lock;auto prior=value_;value_&=value;return prior;
#endif
    }
};
}
