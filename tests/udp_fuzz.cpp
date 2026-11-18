// LLVM libFuzzer entry point: fuzz actual Ethernet/lwIP input with bounded wire
// storage. Persistent stack state deliberately exercises successive packets;
// this is a host sanitizer harness, not part of the target firmware.
#include "../examples/static_service_probe.hpp"
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* bytes,size_t size){
    static nexrt::probe::Wire wire;
    static rtos::net::UdpEcho network;
    static const bool initialized=network.initialize(wire.mac());
    static uint32_t now;
    if(!initialized)__builtin_trap();
    if(size>sizeof(wire.request))return 0;
    memcpy(wire.request,bytes,size);wire.request_size=size;wire.pending=true;
    network.poll(++now,1);return 0;
}
