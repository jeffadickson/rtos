#pragma once
#include "rtos_monitor.hpp"
#if RTOS_TASK_TRACE
namespace rtos::monitor {
/** Thread-owned bounded UART encoder. IO.write consumes bytes synchronously and
 * returns accepted count, never waits. Pending frame offsets survive a full
 * UART, so framing is not corrupted and producers never access the device.
 */
template<class Transport> class Stream {
    uint8_t pending_[34]{};size_t length_{},offset_{};bool delimiter_=true;
public:
    constexpr Stream()=default;
    size_t service(Transport& io,size_t budget=272){
        size_t done=0;
        if(delimiter_&&budget){uint8_t zero=0;if(io.write(&zero,1)!=1)return 0;delimiter_=false;++done;--budget;}
        while(budget){
            if(offset_==length_){Event event;if(!next(event))break;encode(event,pending_,length_);offset_=0;}
            const auto remain=length_-offset_;const auto offer=remain<budget?remain:budget;
            const auto count=io.write(pending_+offset_,offer);if(count>offer)return done;
            offset_+=count;done+=count;budget-=count;if(!count)break;
        }
        return done;
    }
};
}
#endif
