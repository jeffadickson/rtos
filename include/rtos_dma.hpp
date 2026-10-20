#pragma once
/** @file
 * @brief Optional single-CPU, thread-safe DMA staging/ownership layer.
 * See Sphinx dma.rst for BSP callback contracts and buffer/cache restrictions.
 */
#include "rtos_mutex.hpp"
#if !RTOS_DMA
#error "Enable RTOS_DMA to use the DMA module"
#endif
namespace rtos::dma {
enum class Direction : uint8_t { transmit, receive };
enum class Status : uint8_t { ok, pending, busy, invalid, context, stale, device_error, cancelled, quarantined, exhausted };
enum class State : uint8_t { uninitialized, idle, active, completed, failed, quarantined };
enum class Progress : uint8_t { pending, completed, fault };
/// Channel-local generation. Never wraps or resets during this object's lifetime.
struct Ticket { uint32_t generation{}; };
/// BSP must distinguish a stale indication from this transfer's completion.
struct Completion { Ticket ticket{};Progress progress{Progress::pending};size_t bytes{}; };
/** @brief Fixed staging allocation, exclusively reserved for this channel forever.
 * bus is an independently validated DMA address, never inferred from cpu.
 * alignment covers engine AND cache-line requirements; own every touched line.
 */
struct Buffer { uint8_t* cpu{};uint64_t bus{};size_t capacity{},alignment{}; };
/** @brief Boot-bound callbacks; all execute synchronously under a thread mutex.
 * Never block, yield, exit, reenter the channel or retain application pointers.
 * Only staging cpu/bus may remain hardware-owned after start returns true.
 */
struct Backend {
    void* context{};
    bool (*accepts)(void*,Direction,uint64_t,size_t){}; ///< Side-effect-free bus-window/length validation.
    void (*prepare)(void*,Direction,Buffer,size_t){}; ///< Cache preparation + ordering before launch.
    bool (*start)(void*,Direction,uint64_t,size_t,Ticket){}; ///< False guarantees no transfer issued.
    Completion (*poll)(void*){}; ///< Completed guarantees bus quiescence, not merely an IRQ bit.
    bool (*stop)(void*){}; ///< True proves all channel accesses drained; false means uncertain.
    void (*finish)(void*,Direction,Buffer,size_t){}; ///< Post-quiescence cache/acquire operations.
};
struct Snapshot { State state{};Ticket ticket{};size_t requested{},transferred{},capacity{}; };
/** @brief One active operation, no heap, queue, mandatory worker or ISR storage.
 * Construct Channel and backing storage statically. Bind at boot before threads.
 * Runtime calls try the mutex without waiting; concurrent/reentrant callers get
 * busy. Copies/cache callbacks run outside the driver's brief IRQ locks.
 */
class Channel {
    Mutex mutex_;
    Backend backend_{};Buffer buffer_{};Direction direction_{Direction::transmit};
    State state_{State::uninitialized};uint32_t generation_{};size_t requested_{},transferred_{};
    Status begin(const void*,size_t,Ticket&);
    bool matches(Ticket ticket)const{return ticket.generation&&ticket.generation==generation_&&state_!=State::idle&&state_!=State::uninitialized;}
    void release(){state_=State::idle;requested_=transferred_=0;}
public:
    constexpr Channel()=default;
    Channel(const Channel&)=delete;Channel& operator=(const Channel&)=delete;
    /// Boot-only, single caller. Reject bad descriptors unchanged; bind once.
    bool initialize(Backend,Buffer,Direction);
    /// Copy TX data before launch. No source pointer survives this call.
    Status transmit(const void* source,size_t bytes,Ticket& ticket);
    /// Start RX into staging. No destination pointer is accepted until collect.
    Status receive(size_t maximum_bytes,Ticket& ticket);
    /// One bounded backend observation; stale hardware completion cannot release staging.
    Status poll(Ticket);
    /// Copy completed RX or retire TX. Failure preserves output and byte count.
    Status collect(Ticket,void* output,size_t capacity,size_t& bytes);
    /// Stop or quarantine. A failed abort NEVER releases staging for reuse.
    Status cancel(Ticket);
    /// Retry proof of quiescence for quarantined staging; discard uncertain data.
    Status recover(Ticket);
    /// Copy state under the same mutex. Ticket is diagnostic identity, not a security capability.
    Status snapshot(Snapshot&);
};
}
