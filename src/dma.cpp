#include "rtos_dma.hpp"
namespace rtos::dma {
namespace {
Status lock_failure(Result result){return result==Result::context?Status::context:
    (result==Result::owner_dead?Status::quarantined:Status::busy);}
bool overlap(const void* first,size_t a,const void* second,size_t b){
    const auto x=reinterpret_cast<uintptr_t>(first),y=reinterpret_cast<uintptr_t>(second);
    return a&&b&&(x<=y?y-x<a:x-y<b);
}
}
bool Channel::initialize(Backend backend,Buffer buffer,Direction direction){
    if(state_!=State::uninitialized||!buffer.cpu||!buffer.capacity||!buffer.alignment||
       (buffer.alignment&(buffer.alignment-1))||buffer.capacity%buffer.alignment||
       reinterpret_cast<uintptr_t>(buffer.cpu)%buffer.alignment||buffer.bus%buffer.alignment||
       buffer.capacity-1>UINT64_MAX-buffer.bus||
       buffer.capacity-1>UINTPTR_MAX-reinterpret_cast<uintptr_t>(buffer.cpu)||
       overlap(this,sizeof(*this),buffer.cpu,buffer.capacity)||
       (direction!=Direction::transmit&&direction!=Direction::receive)||
       !backend.accepts||!backend.prepare||!backend.start||!backend.poll||!backend.stop||!backend.finish||
       !backend.accepts(backend.context,direction,buffer.bus,buffer.capacity))return false;
    backend_=backend;buffer_=buffer;direction_=direction;state_=State::idle;return true;
}
Status Channel::begin(const void* source,size_t bytes,Ticket& ticket){
    if(state_==State::uninitialized)return Status::invalid;
    if(state_==State::quarantined)return Status::quarantined;
    if(state_!=State::idle)return Status::busy;
    if(!bytes||bytes>buffer_.capacity||!backend_.accepts(backend_.context,direction_,buffer_.bus,bytes)||
       overlap(&ticket,sizeof(ticket),buffer_.cpu,buffer_.capacity)||overlap(&ticket,sizeof(ticket),this,sizeof(*this))||
       (direction_==Direction::transmit&&(!source||
        bytes-1>UINTPTR_MAX-reinterpret_cast<uintptr_t>(source)||
        overlap(source,bytes,&ticket,sizeof(ticket))||
        overlap(source,bytes,buffer_.cpu,buffer_.capacity)||overlap(source,bytes,this,sizeof(*this)))))return Status::invalid;
    if(generation_==UINT32_MAX)return Status::exhausted;
    ++generation_;requested_=bytes;transferred_=0;
    if(source){const auto* from=static_cast<const uint8_t*>(source);for(size_t i=0;i<bytes;++i)buffer_.cpu[i]=from[i];}
    backend_.prepare(backend_.context,direction_,buffer_,bytes);
    if(!backend_.start(backend_.context,direction_,buffer_.bus,bytes,{generation_})){
        backend_.finish(backend_.context,direction_,buffer_,bytes);release();return Status::device_error;
    }
    state_=State::active;ticket={generation_};return Status::ok;
}
Status Channel::transmit(const void* source,size_t bytes,Ticket& ticket){
    LockGuard guard(mutex_);if(!guard.owns_lock())return lock_failure(guard.result());
    if(direction_!=Direction::transmit)return Status::invalid;
    return begin(source,bytes,ticket);
}
Status Channel::receive(size_t bytes,Ticket& ticket){
    LockGuard guard(mutex_);if(!guard.owns_lock())return lock_failure(guard.result());
    if(direction_!=Direction::receive)return Status::invalid;
    return begin(nullptr,bytes,ticket);
}
bool Channel::quiesce(State stopped){
    if(!backend_.stop(backend_.context)){state_=State::quarantined;return false;}
    backend_.finish(backend_.context,direction_,buffer_,requested_);
    state_=stopped;return true;
}
Status Channel::poll(Ticket ticket){
    LockGuard guard(mutex_);if(!guard.owns_lock())return lock_failure(guard.result());
    if(!matches(ticket))return Status::stale;
    if(state_==State::quarantined)return Status::quarantined;
    if(state_==State::completed)return Status::ok;
    if(state_==State::failed)return Status::device_error;
    const auto completion=backend_.poll(backend_.context);
    if(completion.ticket.generation!=generation_)return Status::stale;
    if(completion.progress==Progress::pending)return Status::pending;
    if(completion.progress!=Progress::completed||completion.bytes>requested_||
       (direction_==Direction::transmit&&completion.bytes!=requested_)){
        return quiesce(State::failed)?Status::device_error:Status::quarantined;
    }
    backend_.finish(backend_.context,direction_,buffer_,requested_);
    transferred_=completion.bytes;state_=State::completed;return Status::ok;
}
Status Channel::collect(Ticket ticket,void* output,size_t capacity,size_t& bytes){
    LockGuard guard(mutex_);if(!guard.owns_lock())return lock_failure(guard.result());
    if(!matches(ticket))return Status::stale;
    if(state_==State::quarantined)return Status::quarantined;
    if(state_==State::failed){release();return Status::device_error;}
    if(state_!=State::completed)return Status::pending;
    if(overlap(&bytes,sizeof(bytes),buffer_.cpu,buffer_.capacity)||overlap(&bytes,sizeof(bytes),this,sizeof(*this)))return Status::invalid;
    if(direction_==Direction::receive){
        if(capacity<transferred_||(!output&&transferred_)||
           (transferred_&&(transferred_-1>UINTPTR_MAX-reinterpret_cast<uintptr_t>(output)||
            overlap(output,transferred_,buffer_.cpu,buffer_.capacity)||overlap(output,transferred_,this,sizeof(*this)))))return Status::invalid;
        if(overlap(output,transferred_,&bytes,sizeof(bytes)))return Status::invalid;
        auto* to=static_cast<uint8_t*>(output);for(size_t i=0;i<transferred_;++i)to[i]=buffer_.cpu[i];
    }else if(output||capacity)return Status::invalid;
    bytes=transferred_;release();return Status::ok;
}
Status Channel::cancel(Ticket ticket){
    LockGuard guard(mutex_);if(!guard.owns_lock())return lock_failure(guard.result());
    if(!matches(ticket))return Status::stale;
    if(state_==State::quarantined)return Status::quarantined;
    if(state_==State::active&&!quiesce(State::idle))return Status::quarantined;
    release();return Status::cancelled;
}
Status Channel::recover(Ticket ticket){
    LockGuard guard(mutex_);if(!guard.owns_lock())return lock_failure(guard.result());
    if(!matches(ticket))return Status::stale;
    if(state_!=State::quarantined)return Status::invalid;
    if(!quiesce(State::idle))return Status::quarantined;
    release();return Status::ok;
}
Status Channel::snapshot(Snapshot& output){
    LockGuard guard(mutex_);if(!guard.owns_lock())return lock_failure(guard.result());
    if(overlap(&output,sizeof(output),this,sizeof(*this))||overlap(&output,sizeof(output),buffer_.cpu,buffer_.capacity))return Status::invalid;
    output={state_,{generation_},requested_,transferred_,buffer_.capacity};return Status::ok;
}
}
