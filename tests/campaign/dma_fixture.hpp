#pragma once
#include "rtos_dma.hpp"
namespace campaign_dma {
using namespace rtos::dma;
struct Engine {bool accepts=true,launch=true,stops=true;unsigned prepares{},starts{},polls{},finishes{},stops_called{};Completion result{};};
inline Backend backend(Engine& e){
    return {&e,
      [](void* p,Direction,uint64_t bus,size_t n){return static_cast<Engine*>(p)->accepts&&bus==0x1000&&n<=128;},
      [](void* p,Direction,Buffer,size_t){assert(model::depth==0);static_cast<Engine*>(p)->prepares++;},
      [](void* p,Direction,uint64_t,size_t,Ticket){auto& e=*static_cast<Engine*>(p);e.starts++;return e.launch;},
      [](void* p){auto& e=*static_cast<Engine*>(p);e.polls++;return e.result;},
      [](void* p){auto& e=*static_cast<Engine*>(p);e.stops_called++;return e.stops;},
      [](void* p,Direction,Buffer,size_t){assert(model::depth==0);static_cast<Engine*>(p)->finishes++;}};
}
struct Fixture {
    alignas(64) uint8_t storage[128]{};Channel channel;Engine engine;
    Buffer buffer(){return {storage,0x1000,128,64};}
    bool bind(Direction d=Direction::transmit){return channel.initialize(backend(engine),buffer(),d);}
};
}
