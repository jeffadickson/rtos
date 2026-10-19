#include "rtos_mutex.hpp"
namespace rtos {
void Mutex::attach(Thread& t){owner_=&t;next_owned_=t.owned_mutexes;t.owned_mutexes=this;++t.held_mutexes;}
void Mutex::detach(){
    if(!owner_)return;
    auto** link=&owner_->owned_mutexes;
    while(*link&&*link!=this)link=&(*link)->next_owned_;
    if(*link){*link=next_owned_;--owner_->held_mutexes;}
    owner_=nullptr;next_owned_=nullptr;
}
void Mutex::priority_changed(Thread& start){
    // Recompute from base plus ALL owned mutexes, rather than undoing just one
    // donation. Propagate both rises and falls to the next owner in the wait chain.
    auto* t=&start;
    // Cycles are rejected at enrollment. Bound propagation even after corrupted metadata.
    for(unsigned chain=0;t&&chain<RTOS_MAX_THREADS;++chain){
        uint8_t effective=t->priority;
        unsigned held=0;
        for(auto* m=t->owned_mutexes;m&&held<RTOS_MAX_HELD_MUTEXES;m=m->next_owned_,++held)
            if(auto* waiter=m->waiters_.best())if(waiter->effective_priority>effective)effective=waiter->effective_priority;
        t->effective_priority=effective;
        t=t->waiting_mutex?t->waiting_mutex->owner_:nullptr;
    }
}
void Mutex::changed(void* pointer){auto& mutex=*static_cast<Mutex*>(pointer);if(mutex.owner_)priority_changed(*mutex.owner_);}
Result Mutex::lock(Tick timeout){
    detail::Critical critical;
    auto* self=detail::calling_thread();
    if(!self||self->state!=State::ready)return Result::context;
    if(timeout>MaxWait)return Result::invalid;
    if(faulted_)return Result::owner_dead;
    if(owner_==self)return Result::deadlock;
    if(self->held_mutexes>=RTOS_MAX_HELD_MUTEXES)return Result::overflow;
    if(!owner_){attach(*self);return Result::ok;}
    if(!timeout)return Result::busy;
    // Reject cycles before publishing waiting_mutex; inheritance cannot cure deadlock.
    auto* p=owner_;
    for(unsigned visited=0;p;++visited){
        if(p==self||visited>=RTOS_MAX_THREADS)return Result::deadlock;
        p=p->waiting_mutex?p->waiting_mutex->owner_:nullptr;
    }
    self->waiting_mutex=this;
    auto result=waiters_.wait(timeout);
    self->waiting_mutex=nullptr;
    return result;
}
Result Mutex::unlock(){
    detail::Critical critical;
    auto* self=detail::calling_thread();
    if(!self||owner_!=self)return Result::context;
    const auto before=self->effective_priority;
    detach();
    if(auto* next=waiters_.best()){
        attach(*next); // Grant ownership before making the waiter runnable.
        waiters_.complete(*next,Result::ok);
        priority_changed(*next);critical.changed();
    }
    priority_changed(*self);if(self->effective_priority!=before)critical.changed();return Result::ok;
}
void Mutex::abandon(Thread& thread){
    // A dead owner cannot certify protected state or hardware quiescence. Quarantine
    // ownership and wake every waiter with an explicit fault; recovery is caller policy.
    if(owner_!=&thread)return;
    detach();faulted_=true;
    while(auto* t=waiters_.best())waiters_.complete(*t,Result::owner_dead);
}
void Mutex::owner_exit(Thread& thread){
    detail::abandon_wait(thread);
    while(thread.owned_mutexes)thread.owned_mutexes->abandon(thread);
    priority_changed(thread);
}
Result Mutex::recover_after_quiescence(){
    detail::Critical critical;
    if(!detail::calling_thread())return Result::context;
    if(owner_||waiters_.size())return Result::busy;
    faulted_=false;return Result::ok;
}
MutexInfo Mutex::snapshot()const{detail::Critical critical;return {owner_,waiters_.size(),faulted_};}
}
