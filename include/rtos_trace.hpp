#pragma once
#include "rtos_sync.hpp"
namespace rtos {
struct TraceEvent {Tick tick{};uint32_t code{},first{},second{};};
struct TraceInfo {size_t depth{},capacity{};uint32_t dropped{};};
/** @brief Optional fixed binary trace ring; full drops the new event explicitly.
 * Calls serialize briefly on the single hart. No formatting, heap, device IO,
 * callback or scheduling takes place under the local interrupt mask.
 */
template<size_t Capacity> class TraceRing {
    static_assert(Capacity>0);
    TraceEvent entries_[Capacity]{};size_t head_{},used_{};uint32_t dropped_{};
public:
    constexpr TraceRing()=default;
    TraceRing(const TraceRing&)=delete;
    bool push(const TraceEvent& event){
        detail::Critical lock;
        if(used_==Capacity){if(dropped_!=UINT32_MAX)++dropped_;return false;}
        entries_[(head_+used_)%Capacity]=event;++used_;return true;
    }
    bool pop(TraceEvent& output){
        detail::Critical lock;if(!used_)return false;
        output=entries_[head_];head_=(head_+1)%Capacity;--used_;return true;
    }
    TraceInfo snapshot()const{detail::Critical lock;return {used_,Capacity,dropped_};}
};
}
