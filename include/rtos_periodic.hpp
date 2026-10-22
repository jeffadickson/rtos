#pragma once
#include <stdint.h>
namespace rtos {
/** @brief Fixed-state release grid with constant-time skipped-release counting.
 * Call at intervals shorter than half the 32-bit tick range. The supplied first
 * deadline and all later deadlines share the original grid; no execution drift.
 * This computes a delay only; the caller owns thread context and sleep policy.
 */
struct PeriodicRelease {
    uint32_t next,period,missed{};
    bool advance(uint32_t now,uint32_t& delay){
        if(!period||period>INT32_MAX||now-next==0x80000000u)return false;
        const uint32_t late=now-next;
        if(late<0x80000000u){
            const uint32_t releases=late/period+1,skipped=releases-1;
            missed=skipped>UINT32_MAX-missed?UINT32_MAX:missed+skipped;
            next+=releases*period;
        }
        delay=next-now;return true;
    }
};
}
