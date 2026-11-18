#include "../examples/static_service_probe.hpp"
extern "C" {
#include "lwip/pbuf.h"
#include "lwip/mem.h"
}
#include <cassert>
#include <cstdio>
int main(){
    using namespace nexrt::probe;
    static Wire wire;static rtos::net::UdpEcho echo;
    assert(echo.poll(0)==0);assert(!echo.initialize({}));
    assert(!echo.initialize(wire.mac(),0));assert(echo.initialize(wire.mac()));assert(!echo.initialize(wire.mac()));
    wire.arp();echo.poll(0);assert(wire.response_size>=42&&word(wire.response+12)==0x0806);
    uint8_t payload[513]{};uint32_t seed=0x4e585254;
    for(unsigned trial=0;trial<300;++trial){
        seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;const size_t length=trial==0?512:trial==1?0:seed%513;
        for(size_t i=0;i<length;++i){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;payload[i]=uint8_t(seed);}
        wire.udp(payload,length);echo.poll(trial);assert(wire.echoed(payload,length));
    }
    assert(echo.echoes()==300);
    wire.udp(payload,8);wire.request[42]^=1;echo.poll(300);
    assert(echo.echoes()==300&&wire.response_size==0); // Bad nonzero UDP checksum.
    wire.udp(payload,8,99);echo.poll(300);assert(echo.echoes()==300);
    wire.udp(payload,513);echo.poll(301);assert(echo.echoes()==300&&echo.errors()==1&&wire.response_size==0);
    wire.udp(payload,8);wire.request[24]^=1;echo.poll(302);assert(echo.echoes()==300&&wire.response_size==0);
    wire.udp(payload,8,9);echo.poll(303);assert(echo.echoes()==300);
    wire.udp(payload,8);wire.busy=true;echo.poll(304);assert(echo.echoes()==300&&echo.errors()==2);wire.busy=false;
    // Exhaust actual fixed RX and variable packet pools; no fallback is allowed.
    pbuf* buffers[5]{};for(unsigned i=0;i<4;++i){buffers[i]=pbuf_alloc(PBUF_RAW,128,PBUF_POOL);assert(buffers[i]);}
    assert(!pbuf_alloc(PBUF_RAW,128,PBUF_POOL));
    wire.udp(payload,8);echo.poll(305);assert(echo.echoes()==300);
    for(auto* p:buffers)if(p)pbuf_free(p);
    void* blocks[4]{};for(auto& p:blocks){p=mem_malloc(512);assert(p);}assert(!mem_malloc(512));
    wire.udp(payload,8);echo.poll(306);assert(echo.echoes()==300);
    for(auto* p:blocks)mem_free(p);
    wire.udp(payload,8);echo.poll(306);assert(wire.echoed(payload,8)&&echo.echoes()==301);
    // Reproducible malformed-frame sweep. Ethernet packet lengths stay bounded.
    for(unsigned trial=0;trial<1000;++trial){
        seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
        wire.udp(payload,seed%100);wire.request[14]=uint8_t(seed);wire.request[24]^=0x55;wire.request_size=14+seed%85;
        echo.poll(307+trial);
    }
    std::printf("UDP: 300 seeded wire echoes, exhaustion/recovery, malformed=1000 seed=0x4e585254 passed\n");
}
