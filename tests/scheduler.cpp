#include "scheduler.hpp"
#include <cassert>
#if RTOS_SYNC
#include "sync_model.hpp"
#endif
using namespace rtos;
static void priority(Thread& t,uint8_t value){
    t.priority=value;
#if RTOS_SYNC
    t.effective_priority=value;
#endif
}
int main() {
    detail::Scheduler s;
    assert(s.pick()==nullptr);
    Thread a{}, b{}, c{};
    a.state=b.state=c.state=State::ready;
    priority(a,1); priority(b,2); priority(c,2);
    assert(s.add(a) && s.add(b) && s.add(c) && !s.add(a));
    assert(s.pick()==&b); assert(s.pick()==&c); assert(s.pick()==&b);
    b.state=c.state=State::sleeping; b.wake=2; c.wake=3;
    assert(s.pick()==&a);
    s.advance(2); assert(s.pick()==&b);
    b.state=State::finished; s.advance(1); assert(s.pick()==&c);
    c.state=State::finished; a.state=State::sleeping;
    s.now=0xfffffffeu; a.wake=1;
    s.advance(2); assert(s.pick()==nullptr);
    s.advance(1); assert(s.pick()==&a);
    detail::Scheduler full;
    Thread pool[detail::Scheduler::capacity+1];
    for (unsigned i=0;i<detail::Scheduler::capacity;++i) assert(full.add(pool[i]));
    assert(!full.add(pool[detail::Scheduler::capacity]));
    // Disabling time slicing still permits higher-priority wakeup and explicit yield.
    detail::Scheduler fixed;
    Thread x{}, y{};
    x.state=y.state=State::ready; priority(x,1); priority(y,1);
    assert(fixed.add(x) && fixed.add(y));
    assert(fixed.pick(&x,false)==&x);
    priority(y,2); assert(fixed.pick(&x,false)==&y);
    x.state=State::sleeping; assert(fixed.pick(&x,false)==&y);
    assert(detail::elapsed_ticks(99,100,10)==0);
    assert(detail::elapsed_ticks(100,100,10)==1);
    assert(detail::elapsed_ticks(135,100,10)==4);
    assert(detail::elapsed_ticks(1000000100ull,100,1)==1000000001ull);
    detail::Scheduler bounds;
    Thread control{}; control.stack_begin=0x1000; control.stack_bytes=0x400;
    assert(bounds.add(control));
    assert(bounds.overlaps(0x1100,16));
    assert(!bounds.overlaps(0x1400,16));
    assert(bounds.overlaps_control(reinterpret_cast<uintptr_t>(&control),sizeof(control)));
    assert(bounds.conflicts(0x1100,16));
    assert(bounds.conflicts(reinterpret_cast<uintptr_t>(&control),sizeof(control)));
    assert(!bounds.conflicts(0x1400,16));
    assert(!bounds.overlaps(0x1100,0));
    // Symmetry, containment, exact adjacency and high-address boundaries.
    constexpr uintptr_t starts[]{0,1,16,31,32,UINTPTR_MAX-32,UINTPTR_MAX-16};
    constexpr size_t lengths[]{0,1,16};
    for(auto left:starts)for(auto right:starts)for(auto ln:lengths)for(auto rn:lengths){
        const bool expected=ln&&rn&&left<right+rn&&right<left+ln;
        assert(detail::ranges_overlap(left,ln,right,rn)==expected);
        assert(detail::ranges_overlap(right,rn,left,ln)==expected);
    }
}
