#pragma once
#include "rtos.hpp"
#ifndef RTOS_TASK_TRACE
#define RTOS_TASK_TRACE 0
#endif
namespace rtos::monitor {
enum class Kind:uint8_t {dispatch=1,ready=2,blocked=3,priority=4,mutex_wait=5,mutex_acquire=6,mutex_release=7,finished=8,sleep=9,thread_info=128,clock=129,loss=130};
struct Event {uint64_t time{};uint32_t sequence{},value{};Kind kind{};uint8_t thread{255},related{255},base{},effective{};uint16_t flags{};};
struct Info {uint32_t queued{},capacity{},dropped{};};
/** Single-owner/externally serialized bounded recording storage. Overflow drops
 * the new event and advances its sequence, making omitted history observable.
 */
template<size_t Capacity> class Buffer {
    static_assert(Capacity>0);Event events_[Capacity]{};size_t head_{},used_{};uint32_t sequence_{},dropped_{},reported_{};
public:
    // Unoccupied slots have no semantic identity. Zero them at constant
    // initialization so a static recorder occupies BSS rather than a full
    // initialized ROM image merely because Event's unknown IDs default to255.
    constexpr Buffer(){for(auto& event:events_){event.thread=0;event.related=0;}}
    void push(Event e){e.sequence=sequence_++;if(used_==Capacity){if(dropped_!=UINT32_MAX)++dropped_;return;}events_[(head_+used_)%Capacity]=e;++used_;}
    bool pop(Event& e){if(used_){e=events_[head_];head_=(head_+1)%Capacity;--used_;return true;}
        if(reported_!=dropped_){e={};e.kind=Kind::loss;e.value=dropped_;e.sequence=sequence_++;reported_=dropped_;return true;}return false;}
    Info info()const{return {uint32_t(used_),uint32_t(Capacity),dropped_};}
};
/// NXT1 uses explicit little-endian fields; no C++ structure layout is transmitted.
inline void encode(const Event& event,uint8_t (&wire)[34],size_t& length){
    uint8_t raw[32]{};raw[0]='N';raw[1]='X';raw[2]='T';raw[3]='1';raw[4]=1;raw[5]=uint8_t(event.kind);
    raw[6]=event.thread;raw[7]=event.related;raw[8]=event.base;raw[9]=event.effective;
    raw[10]=uint8_t(event.flags);raw[11]=uint8_t(event.flags>>8);
    for(unsigned i=0;i<4;++i){raw[12+i]=uint8_t(event.sequence>>(8*i));raw[24+i]=uint8_t(event.value>>(8*i));}
    for(unsigned i=0;i<8;++i)raw[16+i]=uint8_t(event.time>>(8*i));
    uint32_t crc=UINT32_MAX;for(unsigned i=0;i<28;++i){crc^=raw[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0x82f63b78u&uint32_t(-int32_t(crc&1)));}crc^=UINT32_MAX;
    for(unsigned i=0;i<4;++i)raw[28+i]=uint8_t(crc>>(8*i));
    size_t at=1,code_at=0;uint8_t code=1;
    for(auto byte:raw){if(byte){wire[at++]=byte;++code;}else{wire[code_at]=code;code_at=at++;code=1;}}
    wire[code_at]=code;wire[at++]=0;length=at;
}
#if RTOS_TASK_TRACE
void record(Kind kind,const Thread* thread,const Thread* related=nullptr,uint32_t value=0);
void publish(Event event);
bool next(Event& event);
Info snapshot();
#endif
}
#if RTOS_TASK_TRACE
#define NEXRT_TRACE(kind,thread,related,value) ::rtos::monitor::record(::rtos::monitor::Kind::kind,thread,related,value)
#else
#define NEXRT_TRACE(kind,thread,related,value) ((void)0)
#endif
