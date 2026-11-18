#include "rtos_monitor_stream.hpp"
#include <cassert>
#include <vector>
// This fixture supplies the consumer boundary without requiring a target HAL.
namespace rtos::monitor {
static Buffer<8> source;
bool next(Event& e){return source.pop(e);}
}
namespace {
struct Transport {
    std::vector<uint8_t> bytes;
    size_t allowance{};
    size_t write(const uint8_t* data,size_t count){
        if(count>allowance)count=allowance;
        bytes.insert(bytes.end(),data,data+count);return count;
    }
};
// Independent COBS and bit-at-a-time CRC-32C reference validates the wire
// contract, including all-zero fields and every byte value in timestamps.
void check(const rtos::monitor::Event& e){
    uint8_t wire[34];size_t count=0;rtos::monitor::encode(e,wire,count);
    assert(count==34&&wire[count-1]==0);
    uint8_t raw[32]{};size_t at=0;
    for(size_t i=0;i<count-1;){const unsigned code=wire[i++];assert(code&&i+code-1<=count-1);
        for(unsigned j=1;j<code;++j){assert(at<32&&wire[i]);raw[at++]=wire[i++];}
        if(i<count-1){assert(at<32);raw[at++]=0;}}
    assert(at==32&&raw[0]=='N'&&raw[3]=='1'&&raw[4]==1);
    assert(raw[5]==uint8_t(e.kind)&&raw[6]==e.thread&&raw[7]==e.related);
    assert(raw[8]==e.base&&raw[9]==e.effective);
    for(unsigned i=0;i<8;++i)assert(raw[16+i]==uint8_t(e.time>>(8*i)));
    for(unsigned i=0;i<4;++i){assert(raw[12+i]==uint8_t(e.sequence>>(8*i)));assert(raw[24+i]==uint8_t(e.value>>(8*i)));}
    uint32_t crc=0xffffffff;
    for(unsigned i=0;i<28;++i){crc^=raw[i];for(unsigned j=0;j<8;++j){bool low=crc&1;crc>>=1;if(low)crc^=0x82f63b78;}}
    crc=~crc;for(unsigned i=0;i<4;++i)assert(raw[28+i]==uint8_t(crc>>(8*i)));
}
}
int main(){
    using namespace rtos::monitor;
    Buffer<2> ring;Event e;
    assert(!ring.pop(e));ring.push({});ring.push({});ring.push({});
    assert(ring.info().queued==2&&ring.info().dropped==1);
    assert(ring.pop(e)&&e.sequence==0);assert(ring.pop(e)&&e.sequence==1);
    assert(ring.pop(e)&&e.kind==Kind::loss&&e.value==1&&e.sequence==3);assert(!ring.pop(e));
    // Ring wrap and deterministic property sweep (seed retained in source).
    uint32_t random=0x4e585431;
    for(unsigned i=0;i<10000;++i){random^=random<<13;random^=random>>17;random^=random<<5;
        e={};e.time=(uint64_t(random)<<32)|~random;e.sequence=random;e.value=~random;
        e.kind=Kind::dispatch;e.thread=uint8_t(random);e.related=uint8_t(random>>8);e.flags=uint16_t(random>>16);
        check(e);ring.push(e);assert(ring.pop(e));assert(!ring.pop(e));}
    Stream<Transport> stream;Transport transport;
    source.push({});source.push({});
    assert(stream.service(transport,0)==0);
    assert(stream.service(transport,10)==0); // UART full, initial delimiter retained.
    transport.allowance=1;
    while(transport.bytes.size()<69)assert(stream.service(transport,3)<=3);
    assert(transport.bytes.size()==69&&transport.bytes[0]==0&&transport.bytes[34]==0&&transport.bytes[68]==0);
    assert(stream.service(transport,272)==0);
}
