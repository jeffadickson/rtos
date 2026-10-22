/** @file Host BSP model: verifies ownership, ordering, address isolation and abort uncertainty. */
#include "sync_model.hpp"
#include "rtos_dma.hpp"
#include <string.h>
using namespace rtos::dma;
namespace {
rtos::Thread first,second;
alignas(64) uint8_t tx_storage[128],rx_storage[128];
Channel tx,rx;
struct Engine {
    Channel* channel{};Buffer buffer{};Ticket ticket{};
    Completion completion{};bool launch=true,stops=true,reenter=false;
    unsigned prepared{},started{},finished{},aborts{};size_t length{};
};
Engine tx_engine,rx_engine;
void contend(Engine& engine){
    assert(model::depth==0);
    if(!engine.reenter)return;
    auto* saved=model::caller;
    // Exercise both same-thread reentry and a different thread while a callback owns the mutex.
    rtos::Thread* callers[]{&first,&second};
    for(auto* caller:callers){
        model::caller=caller;Snapshot untouched{};untouched.capacity=77;
        Ticket unchanged{88};size_t bytes=99;
        assert(engine.channel->snapshot(untouched)==Status::busy&&untouched.capacity==77);
        assert(engine.channel->transmit(nullptr,0,unchanged)==Status::busy&&unchanged.generation==88);
        assert(engine.channel->receive(1,unchanged)==Status::busy&&unchanged.generation==88);
        assert(engine.channel->poll(engine.ticket)==Status::busy);
        assert(engine.channel->collect(engine.ticket,nullptr,0,bytes)==Status::busy&&bytes==99);
        assert(engine.channel->cancel(engine.ticket)==Status::busy);
        assert(engine.channel->recover(engine.ticket)==Status::busy);
    }
    model::caller=saved;
}
bool accepts(void*,Direction,uint64_t bus,size_t bytes){return bus>=0x1000&&bus<0x2000&&bytes<=0x2000-bus;}
void prepare(void* context,Direction,Buffer buffer,size_t bytes){
    auto& engine=*static_cast<Engine*>(context);assert(model::depth==0); // Cache/copy work outside IRQ masking.
    engine.buffer=buffer;engine.length=bytes;++engine.prepared;
    contend(engine);
}
bool start(void* context,Direction,uint64_t bus,size_t bytes,Ticket ticket){
    auto& engine=*static_cast<Engine*>(context);assert(engine.prepared>engine.started);
    assert(bus==engine.buffer.bus&&bytes==engine.length);++engine.started;engine.ticket=ticket;
    contend(engine);
    return engine.launch;
}
Completion poll(void* context){auto& engine=*static_cast<Engine*>(context);contend(engine);return engine.completion;}
bool stop(void* context){auto& engine=*static_cast<Engine*>(context);contend(engine);++engine.aborts;return engine.stops;}
void finish(void* context,Direction,Buffer,size_t){
    assert(model::depth==0);auto& engine=*static_cast<Engine*>(context);contend(engine);++engine.finished;
}
Backend backend(Engine& engine){return {&engine,accepts,prepare,start,poll,stop,finish};}
}
int main(){
    model::use(second);
    model::use(first);assert(model::scheduler.add(first)&&model::scheduler.add(second));
    tx_engine.channel=&tx;rx_engine.channel=&rx;
    Buffer tx_buffer{tx_storage,0x1000,sizeof(tx_storage),64},rx_buffer{rx_storage,0x1100,sizeof(rx_storage),64};
    auto invalid=tx_buffer;invalid.alignment=3;assert(!tx.initialize(backend(tx_engine),invalid,Direction::transmit));
    invalid=tx_buffer;invalid.bus=UINT64_MAX-63;assert(!tx.initialize(backend(tx_engine),invalid,Direction::transmit));
    invalid=tx_buffer;invalid.cpu=tx_storage+1;assert(!tx.initialize(backend(tx_engine),invalid,Direction::transmit));
    auto incomplete=backend(tx_engine);incomplete.finish=nullptr;assert(!tx.initialize(incomplete,tx_buffer,Direction::transmit));
    assert(tx.initialize(backend(tx_engine),tx_buffer,Direction::transmit));
    assert(!tx.initialize(backend(tx_engine),tx_buffer,Direction::transmit));
    assert(rx.initialize(backend(rx_engine),rx_buffer,Direction::receive));
    Ticket ticket{77},other{88};uint8_t source[4]{1,2,3,4};
    assert(tx.transmit(source,0,ticket)==Status::invalid&&ticket.generation==77);
    assert(tx.transmit(source,129,ticket)==Status::invalid&&tx_engine.prepared==0);
    assert(tx.transmit(tx_storage,4,ticket)==Status::invalid);
    assert(tx.transmit(&ticket,sizeof(ticket),ticket)==Status::invalid&&ticket.generation==77);
    assert(tx.receive(4,ticket)==Status::invalid&&rx.transmit(source,4,ticket)==Status::invalid);
    tx_engine.reenter=true;
    assert(tx.transmit(source,4,ticket)==Status::ok);source[0]=99;
    assert(tx_storage[0]==1&&tx.transmit(source,4,other)==Status::busy&&other.generation==88);
    tx_engine.completion={ticket,Progress::pending,0};assert(tx.poll(ticket)==Status::pending);
    tx_engine.completion={{ticket.generation+1},Progress::completed,4};assert(tx.poll(ticket)==Status::stale&&tx_engine.finished==0);
    size_t bytes=99;assert(tx.collect(ticket,nullptr,0,bytes)==Status::pending&&bytes==99);
    tx_engine.completion={ticket,Progress::completed,4};assert(tx.poll(ticket)==Status::ok&&tx_engine.finished==1);
    assert(tx.poll(ticket)==Status::ok&&tx_engine.finished==1);
    assert(tx.collect(ticket,source,4,bytes)==Status::invalid&&bytes==99);
    assert(tx.collect(ticket,nullptr,0,bytes)==Status::ok&&bytes==4);
    assert(tx.poll(ticket)==Status::stale&&tx.cancel(ticket)==Status::stale);
    auto old=ticket;assert(tx.transmit(source,4,ticket)==Status::ok&&old.generation!=ticket.generation);
    assert(tx.cancel(old)==Status::stale);assert(tx.cancel(ticket)==Status::cancelled);
    tx_engine.launch=false;other.generation=88;
    assert(tx.transmit(source,4,other)==Status::device_error&&other.generation==88);
    tx_engine.launch=true;
    assert(rx.receive(64,ticket)==Status::ok);memset(rx_storage,42,64);
    rx_engine.stops=false;assert(rx.cancel(ticket)==Status::quarantined&&rx_engine.finished==0);
    assert(rx.receive(4,other)==Status::quarantined&&rx.poll(ticket)==Status::quarantined);
    uint8_t output[64]{};bytes=99;
    assert(rx.collect(ticket,output,sizeof(output),bytes)==Status::quarantined&&output[0]==0&&bytes==99);
    assert(rx.recover(ticket)==Status::quarantined);
    rx_engine.stops=true;assert(rx.recover(ticket)==Status::ok&&rx_engine.finished==1);
    assert(rx.collect(ticket,output,sizeof(output),bytes)==Status::stale);
    old=ticket;assert(rx.receive(64,ticket)==Status::ok);
    rx_engine.completion={old,Progress::completed,64};assert(rx.poll(ticket)==Status::stale);
    rx_engine.completion={ticket,Progress::completed,7};assert(rx.poll(ticket)==Status::ok);
    assert(rx.collect(ticket,output,6,bytes)==Status::invalid&&bytes==99&&output[0]==0);
    assert(rx.collect(ticket,rx_storage,64,bytes)==Status::invalid);
    assert(rx.collect(ticket,&bytes,sizeof(bytes),bytes)==Status::invalid&&bytes==99);
    assert(rx.collect(ticket,output,64,bytes)==Status::ok&&bytes==7&&output[6]==42&&output[7]==0);
    assert(rx.receive(64,ticket)==Status::ok);
    rx_engine.completion={ticket,Progress::fault,0};assert(rx.poll(ticket)==Status::device_error);
    bytes=91;assert(rx.collect(ticket,output,64,bytes)==Status::device_error&&bytes==91);
    assert(rx.receive(64,ticket)==Status::ok);rx_engine.stops=false;
    rx_engine.completion={ticket,Progress::completed,65};assert(rx.poll(ticket)==Status::quarantined);
    rx_engine.stops=true;assert(rx.recover(ticket)==Status::ok);
    // A short TX completion is an error, even when the engine reports completion.
    const auto tx_finishes=tx_engine.finished,tx_aborts=tx_engine.aborts;
    assert(tx.transmit(source,4,ticket)==Status::ok);
    tx_engine.completion={ticket,Progress::completed,3};
    assert(tx.poll(ticket)==Status::device_error);
    assert(tx_engine.aborts==tx_aborts+1&&tx_engine.finished==tx_finishes+1);
    assert(tx.poll(ticket)==Status::device_error&&tx_engine.finished==tx_finishes+1);
    bytes=91;assert(tx.collect(ticket,nullptr,0,bytes)==Status::device_error&&bytes==91);
    // Empty RX results are valid and can be collected without destination storage.
    assert(rx.receive(64,ticket)==Status::ok);
    rx_engine.completion={ticket,Progress::completed,0};assert(rx.poll(ticket)==Status::ok);
    assert(rx.collect(ticket,nullptr,0,bytes)==Status::ok&&bytes==0);
    // A fault with uncertain stop must retain ownership until the current ticket recovers.
    const auto rx_finishes=rx_engine.finished;
    assert(rx.receive(64,ticket)==Status::ok);rx_engine.stops=false;
    rx_engine.completion={ticket,Progress::fault,0};assert(rx.poll(ticket)==Status::quarantined);
    assert(rx_engine.finished==rx_finishes);
    Snapshot quarantined{};assert(rx.snapshot(quarantined)==Status::ok);
    assert(quarantined.state==State::quarantined&&quarantined.ticket.generation==ticket.generation&&
           quarantined.requested==64&&quarantined.transferred==0);
    bytes=91;memset(output,0,sizeof(output));
    assert(rx.collect(ticket,output,sizeof(output),bytes)==Status::quarantined&&bytes==91&&output[0]==0);
    assert(rx.cancel(ticket)==Status::quarantined&&rx.receive(4,other)==Status::quarantined);
    assert(rx.recover(old)==Status::stale&&rx_engine.finished==rx_finishes);
    assert(rx.recover(ticket)==Status::quarantined&&rx_engine.finished==rx_finishes);
    rx_engine.stops=true;assert(rx.recover(ticket)==Status::ok&&rx_engine.finished==rx_finishes+1);
    assert(rx.recover(ticket)==Status::stale&&rx_engine.finished==rx_finishes+1);
    model::isr=true;Snapshot snap{};assert(tx.snapshot(snap)==Status::context);model::isr=false;
    assert(tx.snapshot(snap)==Status::ok&&snap.state==State::idle&&snap.capacity==128);
    rx_engine.reenter=true;
    // Alternate submitting and supervising threads; no mutex ownership survives an API call.
    for(unsigned iteration=0;iteration<256;++iteration){
        const bool receiving=(iteration&1)!=0;
        auto& channel=receiving?rx:tx;auto& engine=receiving?rx_engine:tx_engine;
        const auto finishes=engine.finished,aborts=engine.aborts;
        model::caller=(iteration&2)?&first:&second;
        const size_t length=1+iteration%sizeof(source);
        assert((receiving?channel.receive(length,ticket):channel.transmit(source,length,ticket))==Status::ok);
        assert(!first.owned_mutexes&&!second.owned_mutexes&&model::depth==0);
        model::caller=model::caller==&first?&second:&first;
        engine.completion={ticket,Progress::pending,0};assert(channel.poll(ticket)==Status::pending);
        if(iteration%8<2){
            engine.stops=false;assert(channel.cancel(ticket)==Status::quarantined&&engine.finished==finishes);
            assert(channel.recover(ticket)==Status::quarantined&&engine.finished==finishes);
            engine.stops=true;assert(channel.recover(ticket)==Status::ok);
            assert(engine.aborts==aborts+3);
        }else{
            if(receiving)memset(rx_storage,int(iteration),length);
            engine.completion={ticket,Progress::completed,length};assert(channel.poll(ticket)==Status::ok);
            bytes=99;
            assert(channel.collect(ticket,receiving?output:nullptr,receiving?sizeof(output):0,bytes)==Status::ok&&bytes==length);
            if(receiving)for(size_t i=0;i<length;++i)assert(output[i]==uint8_t(iteration));
            assert(engine.aborts==aborts);
        }
        assert(engine.finished==finishes+1);
        assert(channel.poll(ticket)==Status::stale&&channel.cancel(ticket)==Status::stale);
        assert(channel.snapshot(snap)==Status::ok&&snap.state==State::idle&&snap.requested==0&&snap.transferred==0);
        assert(!first.owned_mutexes&&!second.owned_mutexes&&model::depth==0);
    }
    assert(model::depth==0&&!first.owned_mutexes&&!second.owned_mutexes);
}
