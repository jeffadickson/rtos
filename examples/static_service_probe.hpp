#pragma once
// Independent wire fixture shared by the host acceptance and target demo. It
// builds Ethernet/IP/UDP bytes directly, without lwIP serializers or checksums.
#include "rtos_udp.hpp"
#include <string.h>
namespace nexrt::probe {
inline uint16_t word(const uint8_t* p){return uint16_t((unsigned(p[0])<<8)|p[1]);}
inline void put(uint8_t* p,uint16_t value){p[0]=uint8_t(value>>8);p[1]=uint8_t(value);}
inline uint32_t sum(const uint8_t* p,size_t n,uint32_t value=0){
    while(n>=2){value+=word(p);p+=2;n-=2;}if(n)value+=unsigned(*p)<<8;return value;
}
inline uint16_t fold(uint32_t value){while(value>>16)value=(value&65535)+(value>>16);return uint16_t(~value);}
struct Wire {
    uint8_t request[1536]{},response[1536]{};size_t request_size{},response_size{};bool pending{},busy{};unsigned sent{};
    rtos::net::Mac mac(){return {this,
        [](void* c,uint8_t* out,size_t capacity,size_t* length){auto& w=*static_cast<Wire*>(c);if(!w.pending)return rtos::net::Io::empty;if(w.request_size>capacity)return rtos::net::Io::error;memcpy(out,w.request,w.request_size);*length=w.request_size;w.pending=false;return rtos::net::Io::ok;},
        [](void* c,const uint8_t* in,size_t n){auto& w=*static_cast<Wire*>(c);if(w.busy)return rtos::net::Io::busy;if(n>sizeof(w.response))return rtos::net::Io::error;memcpy(w.response,in,n);w.response_size=n;++w.sent;return rtos::net::Io::ok;},
        [](void*){return true;}};}
    void arp(){
        memset(request,0,sizeof(request));for(unsigned i=0;i<6;++i)request[i]=255;
        request[6]=2;request[11]=1;put(request+12,0x0806);put(request+14,1);put(request+16,0x0800);
        request[18]=6;request[19]=4;put(request+20,1);request[22]=2;request[27]=1;
        request[28]=192;request[30]=2;request[31]=1;request[38]=192;request[40]=2;request[41]=2;
        request_size=42;response_size=0;pending=true;
    }
    void udp(const uint8_t* payload,size_t n,uint16_t port=7){
        memset(request,0,sizeof(request));request[0]=2;request[5]=2;request[6]=2;request[11]=1;put(request+12,0x0800);
        request[14]=0x45;put(request+16,uint16_t(28+n));request[22]=64;request[23]=17;
        request[26]=192;request[28]=2;request[29]=1;request[30]=192;request[32]=2;request[33]=2;
        put(request+24,fold(sum(request+14,20)));put(request+34,45000);put(request+36,port);put(request+38,uint16_t(8+n));
        memcpy(request+42,payload,n);
        const auto checksum=fold(sum(request+34,8+n,sum(request+26,8)+17+uint32_t(8+n)));
        put(request+40,checksum?checksum:65535); // Computed zero is transmitted as all ones.
        request_size=42+n;response_size=0;pending=true;
    }
    bool echoed(const uint8_t* payload,size_t n)const{
        if(response_size!=42+n||word(response+12)!=0x0800||response[23]!=17||word(response+34)!=7||word(response+36)!=45000||word(response+38)!=8+n)return false;
        if(fold(sum(response+14,20))!=0||memcmp(response+42,payload,n))return false;
        // IPv4 UDP reply checksum includes the independent pseudo header.
        const uint32_t pseudo=sum(response+26,8)+17+uint32_t(8+n);
        return word(response+40)!=0&&fold(sum(response+34,8+n,pseudo))==0;
    }
};
}
