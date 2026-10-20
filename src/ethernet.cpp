#include "rtos_ethernet.hpp"
namespace rtos::net {
// The callback descriptor is intentionally copied into boot-static driver state.
// cppcheck-suppress passedByValue
bool Ethernet::initialize(Mac mac, Input input, void* context) {
    if (initialized_ || !mac.receive || !mac.transmit || !mac.link_up || !input) return false;
    mac_=mac; input_=input; stack_context_=context; initialized_=true; return true;
}
size_t Ethernet::poll(size_t budget) {
    if (!initialized_) return 0;
    size_t delivered=0;
    for (size_t i=0; i<budget; ++i) {
        size_t length=0;
        const auto result=mac_.receive(mac_.context,frame_,sizeof(frame_),&length);
        if (result==Io::empty || result==Io::busy) break;
        if (result!=Io::ok) { ++stats_.errors; break; }
        if (length<14 || length>sizeof(frame_)) { ++stats_.dropped; continue; }
        ++stats_.received;
        if (input_(stack_context_,frame_,length)) ++delivered;
        else ++stats_.dropped;
    }
    return delivered;
}
Io Ethernet::send(const uint8_t* frame, size_t length) {
    if (!initialized_ || !frame || length<14 || length>frame_capacity) return Io::error;
    const auto result=mac_.transmit(mac_.context,frame,length);
    if (result==Io::ok) ++stats_.transmitted;
    else if (result==Io::busy) ++stats_.busy;
    else ++stats_.errors;
    return result;
}
bool Ethernet::link_up() const { return initialized_ && mac_.link_up(mac_.context); }
}
