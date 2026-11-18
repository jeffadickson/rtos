#include "rtos_virtio_net.hpp"
#include <cassert>
#include <cstring>
using namespace rtos::net;
// The model owns device completions independently of the driver's private
// indices, reconstructing DMA addresses only from published MMIO registers.
struct Model {
    uint32_t status{},selector{},queue{},maximum=8,version=2,features=1;
    uintptr_t addresses[2][3]{};
    bool ready[2]{};
    static uint32_t read(void* c,uint32_t o){auto& m=*static_cast<Model*>(c);
        switch(o){case 0:return 0x74726976;case 4:return m.version;case 8:return 1;
        case 0x10:return m.selector?m.features:32;case 0x34:return m.maximum;
        case 0x44:return m.ready[m.queue];case 0x70:return m.status;default:return 0;}}
    static void write(void* c,uint32_t o,uint32_t v){auto& m=*static_cast<Model*>(c);
        if(o==0x70)m.status=v;else if(o==0x14)m.selector=v;else if(o==0x30)m.queue=v;
        else if(o==0x44)m.ready[m.queue]=v;
        else if(o>=0x80&&o<=0xa4){unsigned i=(o-0x80)/16;
            if(i<3){if(o&4)m.addresses[m.queue][i]|=uintptr_t(uint64_t(v)<<32);else m.addresses[m.queue][i]=v;}}}
    VirtioIo io(){return {this,read,write};}
    virtio::Descriptor* descriptors(unsigned q){return reinterpret_cast<virtio::Descriptor*>(addresses[q][0]);}
    virtio::Available& available(unsigned q){return *reinterpret_cast<virtio::Available*>(addresses[q][1]);}
    virtio::Used& used(unsigned q){return *reinterpret_cast<virtio::Used*>(addresses[q][2]);}
    void complete(unsigned q,uint32_t id,uint32_t size){auto& u=used(q);u.ring[u.index%8]={id,size};++u.index;}
    void receive(unsigned id,const uint8_t* bytes,size_t size,uint8_t flags=0){
        auto* p=reinterpret_cast<uint8_t*>(uintptr_t(descriptors(0)[id].address));
        std::memset(p,0,12);p[0]=flags;std::memcpy(p+12,bytes,size);complete(0,id,uint32_t(size+12));}
};
int main(){
    VirtioNet missing;assert(!missing.initialize({}));assert(!missing.initialize_mmio(1));
    for(unsigned bad=0;bad<3;++bad){Model m;VirtioNet n;if(bad==0)m.version=1;if(bad==1)m.features=0;if(bad==2)m.maximum=7;assert(!n.initialize(m.io()));assert(!n.link_up());}
    Model m;VirtioNet n;assert(n.initialize(m.io()));assert(n.link_up());assert(!n.initialize(m.io()));
    uint8_t frame[1536]{},out[1536]{};size_t got=999;
    assert(n.receive(out,sizeof out,&got)==Io::empty);
    assert(n.transmit(nullptr,14)==Io::error);assert(n.transmit(frame,13)==Io::error);
    assert(n.transmit(frame,sizeof frame+1)==Io::error);
    for(unsigned i=0;i<sizeof frame;++i)frame[i]=uint8_t(i);
    assert(n.transmit(frame,64)==Io::ok);assert(n.transmit(frame,64)==Io::busy);
    auto* tx=reinterpret_cast<uint8_t*>(uintptr_t(m.descriptors(1)[0].address));
    assert(m.descriptors(1)[0].length==76&&std::memcmp(tx+12,frame,64)==0);
    frame[0]=77;assert(tx[12]==0); // Copy survives caller reuse.
    m.complete(1,0,0);assert(n.transmit(frame,64)==Io::ok);m.complete(1,0,0);
    // More than 65536 completions exercise both avail and used 16-bit wrap.
    uint32_t seed=0x514e4554;
    for(unsigned i=0;i<66000;++i){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
        unsigned id=i%8;size_t size=14+seed%(sizeof frame-13);frame[0]=uint8_t(seed);
        m.receive(id,frame,size);assert(n.receive(out,sizeof out,&got)==Io::ok);
        assert(got==size&&std::memcmp(frame,out,size)==0);}
    m.receive(0,frame,64,1);assert(n.receive(out,sizeof out,&got)==Io::error);assert(n.link_up());
    m.receive(0,frame,64);assert(n.receive(out,63,&got)==Io::error);assert(n.link_up());
    m.complete(0,8,76);assert(n.receive(out,sizeof out,&got)==Io::error);assert(!n.link_up());
    assert(n.statistics().received==66000&&n.statistics().busy==1);
    Model bad;VirtioNet broken;assert(broken.initialize(bad.io()));
    bad.used(0).index=9;assert(broken.receive(out,sizeof out,&got)==Io::error);
    Model txbad;VirtioNet txbroken;assert(txbroken.initialize(txbad.io()));
    assert(txbroken.transmit(frame,64)==Io::ok);txbad.complete(1,1,0);
    assert(txbroken.transmit(frame,64)==Io::error);
}
