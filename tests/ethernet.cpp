#include "rtos_ethernet.hpp"
#include <cassert>
using namespace rtos::net;
struct Fake { unsigned remaining{8}; bool accept{true}; bool malformed{}; Io tx{Io::ok}, rx{Io::ok}; };
Io rx(void* p, uint8_t* bytes, size_t capacity, size_t* length) {
    auto& f=*static_cast<Fake*>(p);
    assert(capacity==frame_capacity);
    if (f.rx!=Io::ok) return f.rx;
    if (!f.remaining) return Io::empty;
    --f.remaining; bytes[0]=0x42; *length=f.malformed ? capacity+1 : 64;
    return Io::ok;
}
Io tx(void* p,const uint8_t*,size_t) { return static_cast<Fake*>(p)->tx; }
bool link(void*) { return true; }
bool input(void* p,const uint8_t* bytes,size_t length) {
    assert(bytes[0]==0x42 && length==64); return static_cast<Fake*>(p)->accept;
}
int main() {
    Ethernet net;
    assert(!net.link_up() && net.poll()==0);
    assert(!net.initialize({},input,nullptr));
    Fake fake;
    assert(net.initialize({&fake,rx,tx,link},input,&fake));
    assert(!net.initialize({&fake,rx,tx,link},input,&fake));
    assert(net.link_up());
    assert(net.poll(0)==0 && fake.remaining==8);
    assert(net.poll(2)==2 && fake.remaining==6);
    fake.accept=false; assert(net.poll(1)==0);
    fake.malformed=true; assert(net.poll(1)==0);
    assert(net.statistics().received==3 && net.statistics().dropped==2);
    fake.rx=Io::busy; assert(net.poll(3)==0 && fake.remaining==4);
    fake.rx=Io::error; assert(net.poll(3)==0 && net.statistics().errors==1);
    uint8_t frame[64]{};
    assert(net.send(nullptr,64)==Io::error);
    assert(net.send(frame,13)==Io::error);
    assert(net.send(frame,frame_capacity+1)==Io::error);
    assert(net.send(frame,sizeof(frame))==Io::ok);
    fake.tx=Io::busy; assert(net.send(frame,sizeof(frame))==Io::busy);
    fake.tx=Io::error; assert(net.send(frame,sizeof(frame))==Io::error);
    assert(net.statistics().transmitted==1 && net.statistics().busy==1 && net.statistics().errors==2);
}
