// Actual ETL 20.40.0 pool, with exceptions/RTTI disabled. This is an evaluation
// fixture, not a replacement for NexRT generation/ownership semantics.
#include "etl/pool.h"
#include <cassert>
#include <cstdint>
struct alignas(32) Packet { uint32_t sequence;uint8_t bytes[60]; };
int main(){
    etl::pool<Packet,4> pool;
    Packet* packets[4]{};
    for(unsigned i=0;i<4;++i){assert(!pool.full());packets[i]=pool.create();assert(reinterpret_cast<uintptr_t>(packets[i])%alignof(Packet)==0);}
    assert(pool.full()&&pool.available()==0);
    // Guard exhaustion explicitly: unchecked create would invoke ETL's error policy.
    pool.release(packets[1]);
    assert(pool.available()==1);
    auto* reused=pool.create();assert(reused==packets[1]);pool.release(reused);
    pool.release(packets[0]);pool.release(packets[2]);pool.release(packets[3]);assert(pool.empty());
}
