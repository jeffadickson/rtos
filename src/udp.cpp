#include "rtos_udp.hpp"
extern "C" {
#include "lwip/init.h"
#include "lwip/etharp.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
}
namespace { uint32_t clock_ms;bool stack_claimed; }
extern "C" u32_t sys_now(){return clock_ms;}
namespace rtos::net {
err_t UdpEcho::configure(netif* n){
    n->name[0]='n';n->name[1]='x';n->output=etharp_output;n->linkoutput=output;
    n->mtu=1500;n->hwaddr_len=6;
    const uint8_t mac[6]={2,0,0,0,0,2};for(unsigned i=0;i<6;++i)n->hwaddr[i]=mac[i];
    n->flags=NETIF_FLAG_BROADCAST|NETIF_FLAG_ETHARP|NETIF_FLAG_ETHERNET;return ERR_OK;
}
err_t UdpEcho::output(netif* n,pbuf* packet){
    auto& self=*static_cast<UdpEcho*>(n->state);
    if(packet->tot_len>sizeof(self.transmit_))return ERR_BUF;
    if(pbuf_copy_partial(packet,self.transmit_,packet->tot_len,0)!=packet->tot_len)return ERR_BUF;
    const auto result=self.ethernet_.send(self.transmit_,packet->tot_len);
    return result==Io::ok?ERR_OK:result==Io::busy?ERR_WOULDBLOCK:ERR_IF;
}
bool UdpEcho::input(void* context,const uint8_t* bytes,size_t length){
    auto& self=*static_cast<UdpEcho*>(context);
    pbuf* packet=pbuf_alloc(PBUF_RAW,u16_t(length),PBUF_POOL);
    if(!packet)return false;
    if(pbuf_take(packet,bytes,u16_t(length))!=ERR_OK){pbuf_free(packet);return false;}
    // On success the stack consumes the pbuf; on error ownership remains here.
    if(ethernet_input(packet,&self.interface_)!=ERR_OK){pbuf_free(packet);return false;}
    return true;
}
void UdpEcho::received(void* context,udp_pcb* pcb,pbuf* packet,const ip_addr_t* address,u16_t port){
    auto& self=*static_cast<UdpEcho*>(context);
    const u16_t length=packet->tot_len;
    const ip_addr_t peer=*address; // Do not retain an address borrowed from RX processing.
    if(length>sizeof(self.payload_)){++self.errors_;pbuf_free(packet);return;}
    const auto copied=pbuf_copy_partial(packet,self.payload_,length,0);
    pbuf_free(packet); // Release RX pool before obtaining a reply buffer.
    if(copied!=length){++self.errors_;return;}
    pbuf* reply=pbuf_alloc(PBUF_TRANSPORT,length,PBUF_RAM);
    if(!reply){++self.errors_;return;}
    const err_t loaded=pbuf_take(reply,self.payload_,length);
    const err_t sent=loaded==ERR_OK?udp_sendto(pcb,reply,&peer,port):loaded;
    pbuf_free(reply);
    if(sent==ERR_OK)++self.echoes_;else ++self.errors_;
}
bool UdpEcho::initialize(Mac mac,uint16_t port){
    if(initialized_||stack_claimed||!port||!mac.receive||!mac.transmit||!mac.link_up)return false;
    if(!ethernet_.initialize(mac,input,this))return false;
    stack_claimed=true;lwip_init();
    ip4_addr_t ip,mask,gateway;IP4_ADDR(&ip,192,0,2,2);IP4_ADDR(&mask,255,255,255,0);IP4_ADDR(&gateway,0,0,0,0);
    if(!netif_add(&interface_,&ip,&mask,&gateway,this,configure,ethernet_input))return false;
    pcb_=udp_new();
    if(!pcb_){netif_remove(&interface_);return false;}
    if(udp_bind(pcb_,IP_ADDR_ANY,port)!=ERR_OK){udp_remove(pcb_);pcb_=nullptr;netif_remove(&interface_);return false;}
    udp_recv(pcb_,received,this);netif_set_default(&interface_);netif_set_up(&interface_);
    if(ethernet_.link_up())netif_set_link_up(&interface_);
    initialized_=true;return true;
}
size_t UdpEcho::poll(uint32_t now_ms,size_t budget){
    if(!initialized_)return 0;
    clock_ms=now_ms;
    if(ethernet_.link_up())netif_set_link_up(&interface_);else netif_set_link_down(&interface_);
    const auto count=ethernet_.poll(budget);sys_check_timeouts();return count;
}
}
