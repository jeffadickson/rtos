#pragma once
#include "rtos_sync.hpp"
namespace rtos {
struct FaultRecord {uint32_t cause{},pc{},value{},status{},seed{},worker{},sequence{};};
/** @brief Boot-static first-failure capture for ordinary readable RAM.
 * capture is valid in thread/ISR context when the port's local mask is usable.
 * It is not an NMI/double-fault, reset-retention or radiation-qualified recorder.
 * No reset operation exists: a later failure cannot erase the initial evidence.
 */
class FirstFault {
    FaultRecord record_{};bool present_{};
public:
    constexpr FirstFault()=default;
    FirstFault(const FirstFault&)=delete;
    bool capture(const FaultRecord& record){detail::Critical lock;if(present_)return false;record_=record;present_=true;return true;}
    bool snapshot(FaultRecord& output)const{detail::Critical lock;if(!present_)return false;output=record_;return true;}
};
}
