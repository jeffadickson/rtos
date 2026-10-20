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
bool accepts(void*,Direction,uint64_t bus,size_t bytes){return bus>=0x1000&&bus<0x2000&&bytes<=0x2000-bus;}
void prepare(void* context,Direction,Buffer buffer,size_t bytes){
    auto& engine=*static_cast<Engine*>(context);assert(model::depth==0); // Cache/copy work outside IRQ masking.
    engine.buffer=buffer;engine.length=bytes;++engine.prepared;
}
bool start(void* context,Direction,uint64_t bus,size_t bytes,Ticket ticket){
    auto& engine=*static_cast<Engine*>(context);assert(engine.prepared>engine.started);
    assert(bus==engine.buffer.bus&&bytes==engine.length);++engine.started;engine.ticket=ticket;
    if(engine.reenter){
        model::caller=&second;Snapshot untouched{};untouched.capacity=77;
        assert(engine.channel->snapshot(untouched)==Status::busy&&untouched.capacity==77);model::caller=&first;
    }
    return engine.launch;
}
Completion poll(void* context){return static_cast<Engine*>(context)->completion;}
bool stop(void* context){auto& engine=*static_cast<Engine*>(context);++engine.aborts;return engine.stops;}
void finish(void* context,Direction,Buffer,size_t){assert(model::depth==0);++static_cast<Engine*>(context)->finished;}
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
    model::isr=true;Snapshot snap{};assert(tx.snapshot(snap)==Status::context);model::isr=false;
    assert(tx.snapshot(snap)==Status::ok&&snap.state==State::idle&&snap.capacity==128);
    assert(model::depth==0&&!first.owned_mutexes&&!second.owned_mutexes);
}
