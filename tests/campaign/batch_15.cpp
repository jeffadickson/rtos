#include "common.hpp"
#include "rtos_mutex.hpp"
using namespace rtos;
static Mutex* active;static Thread* owner;static Thread waiter;
static void inherit(){model::caller=owner;assert(owner->effective_priority==9);assert(active->unlock()==Result::ok);}
CASE(141,"NXR-016","mutex ownership metadata released together"){Mutex m;auto* self=model::caller;assert(m.lock()==Result::ok&&self->held_mutexes==1&&m.snapshot().owner==self);assert(m.unlock()==Result::ok&&!self->held_mutexes&&!m.snapshot().owner);}
CASE(142,"NXR-018","recursive finite wait rejected before enrollment"){Mutex m;assert(m.lock()==Result::ok&&m.lock(3)==Result::deadlock&&!m.snapshot().waiters);m.unlock();}
CASE(143,"NXR-016","nonblocking contention does not donate priority"){Mutex m;auto* self=model::caller;m.lock();Thread other;model::use(other,9);assert(m.lock()==Result::busy&&self->effective_priority==self->priority);model::caller=self;m.unlock();}
CASE(144,"NXR-016","foreign unlock leaves ownership intact"){Mutex m;auto* self=model::caller;m.lock();Thread other;model::use(other);assert(m.unlock()==Result::context&&m.snapshot().owner==self);model::caller=self;m.unlock();}
CASE(145,"NXR-018","recovery cannot steal live ownership"){Mutex m;m.lock();assert(m.recover_after_quiescence()==Result::busy&&m.snapshot().owner==model::caller);m.unlock();}
CASE(146,"NXR-018","owner termination quarantines mutex"){Mutex m;m.lock();Mutex::owner_exit(*model::caller);assert(m.snapshot().owner_dead&&!m.snapshot().owner&&m.lock()==Result::owner_dead);}
CASE(147,"NXR-018","explicit quiescent recovery permits reacquisition"){Mutex m;m.lock();Mutex::owner_exit(*model::caller);assert(m.recover_after_quiescence()==Result::ok&&m.lock()==Result::ok);m.unlock();}
CASE(148,"NXR-016","failed guard never unlocks another owner"){Mutex m;auto* self=model::caller;m.lock();Thread other;model::use(other);{LockGuard g(m);assert(!g.owns_lock()&&g.result()==Result::busy);}assert(m.snapshot().owner==self);model::caller=self;m.unlock();}
CASE(149,"NXR-016","mutex ownership budget enforced without partial attachment"){Mutex m[RTOS_MAX_HELD_MUTEXES+1];for(unsigned i=0;i<RTOS_MAX_HELD_MUTEXES;i++)assert(m[i].lock()==Result::ok);assert(m[RTOS_MAX_HELD_MUTEXES].lock()==Result::overflow&&!m[RTOS_MAX_HELD_MUTEXES].snapshot().owner);for(unsigned i=0;i<RTOS_MAX_HELD_MUTEXES;i++)m[i].unlock();}
CASE(150,"NXR-017","priority donation precedes park and unwinds on handoff"){Mutex m;owner=model::caller;active=&m;m.lock();model::use(waiter,9);model::scheduler.add(waiter);model::on_park=inherit;assert(m.lock(3)==Result::ok&&m.snapshot().owner==&waiter&&owner->effective_priority==owner->priority);m.unlock();model::caller=owner;owner=nullptr;active=nullptr;model::on_park=nullptr;}
