#include "rtos_virtio_net.hpp"
#if defined(_MSC_VER)
#include <intrin.h>
#endif
namespace rtos::net {
namespace {
// Hardware memory ordering is separate from volatile register visibility. The
// QEMU profile uses coherent flat RAM; real cached/DMA targets need another BSP.
void fence(){
#if defined(__riscv)
    asm volatile("fence iorw,iorw" ::: "memory");
#elif defined(_MSC_VER)
    _ReadWriteBarrier();
#else
    __sync_synchronize();
#endif
}
void increment(uint32_t& value){if(value!=UINT32_MAX)++value;}
}
uint32_t VirtioNet::read(uint32_t offset)const{return io_.read(io_.context,offset);}
void VirtioNet::write(uint32_t offset,uint32_t value){io_.write(io_.context,offset,value);}
bool VirtioNet::fail(){ready_=false;increment(statistics_.errors);if(io_.write)write(0x70,read(0x70)|128u);return false;}
bool VirtioNet::configure_queue(unsigned index,virtio::Queue& q){
    write(0x30,index);
    if(read(0x34)<virtio::queue_size||read(0x44))return false;
    write(0x38,virtio::queue_size);
    auto address=[&](uint32_t offset,const void* pointer){uint64_t a=reinterpret_cast<uintptr_t>(pointer);write(offset,uint32_t(a));write(offset+4,uint32_t(a>>32));};
    address(0x80,q.descriptors);address(0x90,&q.available);address(0xa0,&q.used);
    q.available.flags=1; // Suppress queue IRQs: the owner explicitly polls.
    fence();write(0x44,1);return true;
}
bool VirtioNet::initialize(VirtioIo io){
    if(attempted_||!io.read||!io.write)return false;
    attempted_=true;io_=io;
    // Until identity/version are established, no device register may be
    // written: a legacy or unrelated transport has a different contract.
    if(read(0)!=0x74726976||read(4)!=2||read(8)!=1){increment(statistics_.errors);return false;}
    write(0x70,0);
    unsigned reset=0;while(read(0x70)&&++reset<1024){}
    if(read(0x70))return fail();
    write(0x70,3); // ACKNOWLEDGE | DRIVER, before feature negotiation.
    write(0x14,1);if(!(read(0x10)&1))return fail(); // VERSION_1 is mandatory.
    write(0x14,0);if(!(read(0x10)&(1u<<5)))return fail(); // Device MAC config.
    write(0x24,1);write(0x20,1);write(0x24,0);write(0x20,1u<<5);
    write(0x70,11);if(!(read(0x70)&8))return fail(); // Device must accept features.
    for(unsigned i=0;i<virtio::queue_size;++i){
        receive_.descriptors[i]={reinterpret_cast<uintptr_t>(receive_bytes_[i]),sizeof(receive_bytes_[i]),2,0};
        receive_.available.ring[i]=uint16_t(i);
    }
    receive_.available.index=virtio::queue_size;
    transmit_.descriptors[0].address=reinterpret_cast<uintptr_t>(transmit_bytes_);
    if(!configure_queue(0,receive_)||!configure_queue(1,transmit_))return fail();
    fence();write(0x70,15);ready_=true;write(0x50,0);return true;
}
bool VirtioNet::initialize_mmio(uintptr_t base){
    if((base&4095)||base>UINTPTR_MAX-0x200)return false;
    return initialize({reinterpret_cast<void*>(base),
        [](void* c,uint32_t o){return *reinterpret_cast<volatile uint32_t*>(reinterpret_cast<uintptr_t>(c)+o);},
        [](void* c,uint32_t o,uint32_t v){*reinterpret_cast<volatile uint32_t*>(reinterpret_cast<uintptr_t>(c)+o)=v;}});
}
bool VirtioNet::reap_transmit(){
    const auto used=reinterpret_cast<volatile virtio::Used*>(&transmit_.used)->index;
    if(used==transmit_used_)return true;
    fence();
    if(!transmitting_||uint16_t(used-transmit_used_)!=1||transmit_.used.ring[transmit_used_%virtio::queue_size].id!=0)return fail();
    transmit_used_=used;transmitting_=false;return true;
}
void VirtioNet::repost(uint16_t id){
    receive_.available.ring[receive_.available.index%virtio::queue_size]=id;
    fence();receive_.available.index=uint16_t(receive_.available.index+1);fence();write(0x50,0);
}
Io VirtioNet::receive(uint8_t* out,size_t capacity,size_t* length){
    if(!ready_||!out||!length||!reap_transmit())return Io::error;
    const auto used=reinterpret_cast<volatile virtio::Used*>(&receive_.used)->index;
    if(used==receive_used_)return Io::empty;
    fence();
    if(uint16_t(used-receive_used_)>virtio::queue_size){fail();return Io::error;}
    const auto entry=receive_.used.ring[receive_used_%virtio::queue_size];
    if(entry.id>=virtio::queue_size||entry.length<virtio::header_bytes||entry.length>sizeof(receive_bytes_[0])){fail();return Io::error;}
    auto* data=receive_bytes_[entry.id];const size_t size=entry.length-virtio::header_bytes;
    receive_used_=uint16_t(receive_used_+1);
    // We negotiate neither checksum offload nor GSO; accepting such headers
    // would otherwise hand the stack an unchecked or partial packet.
    if(data[0]||data[1]||size>capacity){repost(uint16_t(entry.id));increment(statistics_.errors);return Io::error;}
    for(size_t i=0;i<size;++i)out[i]=data[virtio::header_bytes+i];
    *length=size;repost(uint16_t(entry.id));increment(statistics_.received);
    const auto pending=read(0x60);if(pending)write(0x64,pending);
    return Io::ok;
}
Io VirtioNet::transmit(const uint8_t* bytes,size_t size){
    if(!ready_||!bytes||size<14||size>frame_capacity||!reap_transmit())return Io::error;
    if(transmitting_){increment(statistics_.busy);return Io::busy;}
    for(unsigned i=0;i<virtio::header_bytes;++i)transmit_bytes_[i]=0;
    for(size_t i=0;i<size;++i)transmit_bytes_[virtio::header_bytes+i]=bytes[i];
    transmit_.descriptors[0].length=uint32_t(size+virtio::header_bytes);
    transmit_.available.ring[transmit_.available.index%virtio::queue_size]=0;
    fence();transmit_.available.index=uint16_t(transmit_.available.index+1);transmitting_=true;fence();write(0x50,1);
    increment(statistics_.transmitted);return Io::ok;
}
Mac VirtioNet::mac(){return {this,
    [](void* c,uint8_t* p,size_t n,size_t* got){return static_cast<VirtioNet*>(c)->receive(p,n,got);},
    [](void* c,const uint8_t* p,size_t n){return static_cast<VirtioNet*>(c)->transmit(p,n);},
    [](void* c){return static_cast<VirtioNet*>(c)->link_up();}};}
}
