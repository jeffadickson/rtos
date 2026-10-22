#include "machine.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace nexrt::rv {
namespace {
constexpr uint32_t sx(uint32_t value,unsigned bits) { const uint32_t sign=uint32_t(1)<<(bits-1);return (value^sign)-sign; }
constexpr int32_t si(uint32_t value) { return std::bit_cast<int32_t>(value); }
constexpr uint32_t enc_i(unsigned op,unsigned f,unsigned d,unsigned a,uint32_t v) { return ((v&4095)<<20)|(a<<15)|(f<<12)|(d<<7)|op; }
constexpr uint32_t enc_r(unsigned f7,unsigned f,unsigned d,unsigned a,unsigned b) { return (f7<<25)|(b<<20)|(a<<15)|(f<<12)|(d<<7)|0x33; }
constexpr uint32_t enc_s(unsigned a,unsigned b,uint32_t v) { return ((v&0xfe0)<<20)|(b<<20)|(a<<15)|(2<<12)|((v&31)<<7)|0x23; }
constexpr uint32_t enc_j(unsigned d,uint32_t v) { return ((v&0x100000)<<11)|((v&0x7fe)<<20)|((v&0x800)<<9)|(v&0xff000)|(d<<7)|0x6f; }
constexpr uint32_t enc_b(unsigned f,unsigned a,uint32_t v) { return ((v&0x1000)<<19)|((v&0x7e0)<<20)|(a<<15)|(f<<12)|((v&30)<<7)|((v&0x800)>>4)|0x63; }
uint32_t little(const uint8_t* p,unsigned bytes) { uint32_t v=0;std::memcpy(&v,p,bytes);if constexpr(std::endian::native==std::endian::big){uint32_t b=0;for(unsigned i=0;i<bytes;i++)b|=uint32_t(p[i])<<(i*8);return b;}return v; }
void put_little(uint8_t* p,unsigned bytes,uint32_t value) { if constexpr(std::endian::native==std::endian::little)std::memcpy(p,&value,bytes);else for(unsigned i=0;i<bytes;i++)p[i]=uint8_t(value>>(8*i)); }
uint64_t clmul(uint32_t a,uint32_t b) { uint64_t result=0;while(b){unsigned bit=std::countr_zero(b);result^=uint64_t(a)<<bit;b&=b-1;}return result; }
}
Machine::Machine(Profile p):profile(p),ram(),cache() {
    if(p.ram_bytes<4||p.ram_bytes>64u*1024u*1024u||!std::has_single_bit(p.ram_bytes)||!p.cycles_per_instruction)
        throw std::invalid_argument("RAM must be a power of two from 4 bytes to 64 MiB; cycle cost must be nonzero");
    ram.resize(p.ram_bytes);cache.resize(std::min(p.ram_bytes/2,size_t(16384)));
}
uint32_t Machine::peek32(uint32_t address) const {
    if(uint64_t(address)+4>ram.size())throw std::out_of_range("Host RAM read");
    return little(ram.data()+address,4);
}
void Machine::poke32(uint32_t address,uint32_t value) {
    if(uint64_t(address)+4>ram.size())throw std::out_of_range("Host RAM write");
    put_little(ram.data()+address,4,value);
}
bool Machine::effective_user(bool execute) const { return user||(!execute&&(mstatus&(1u<<17))&&((mstatus>>11)&3)==0); }
bool Machine::permits(uint32_t address,unsigned bytes,bool read,bool write,bool execute) const {
    if(!profile.user_pmp)return true;
    const bool u=effective_user(execute);
    const uint64_t first=address,last=first+bytes;
    if(!bytes||last>0x100000000ull)return false;
    // All entries OFF is the common M-mode startup path; U still denies.
    if(pmpcfg==0)return !u;
    uint64_t previous=0;
    for(unsigned i=0;i<4;i++){
        const uint32_t c=(pmpcfg>>(8*i))&255;
        const uint64_t encoded=pmpaddr[i];uint64_t lo=0,hi=0;
        switch((c>>3)&3){
        case 1:lo=previous<<2;hi=encoded<<2;break;
        case 2:lo=encoded<<2;hi=lo+4;break;
        case 3:{unsigned ones=std::countr_one(uint32_t(encoded));ones=std::min(ones,30u);const uint64_t size=uint64_t(1)<<(ones+3);lo=(encoded<<2)&~(size-1);hi=lo+size;break;}
        default:break;
        }
        previous=encoded;
        if(((c>>3)&3)&&first<hi&&last>lo)
            return first>=lo&&last<=hi&&((!u&&!(c&128))||((!read||(c&1))&&(!write||(c&2))&&(!execute||(c&4))));
    }
    return !u;
}
uint32_t Machine::pending() const { return (msip?8u:0u)|(mtime>=mtimecmp?128u:0u)|(external_irq?2048u:0u); }
void Machine::trap(uint32_t cause,uint32_t value) {
    last_trap={cause,value,pc,last_trap.sequence+1};
    if(!(cause&0x80000000u)&&cause!=8&&cause!=11){faults++;last_fault=last_trap;}
    mepc=pc&~uint32_t(profile.compressed?1:3);mcause=cause;mtval=value;
    const uint32_t saved=(mstatus&8)?128:0;
    mstatus=(mstatus&~(0x1888u))|saved|(user?0:0x1800);
    user=false;pc=mtvec&~3u;
    if(cause==0x80000007)timer_interrupts++;
    if(cause==0x80000003)software_interrupts++;
    if(cause==8||cause==11)ecalls++;
}
bool Machine::load(uint32_t address,unsigned bytes,uint32_t& value,bool execute) {
    if(address&(bytes-1)){trap(execute?0:4,address);return false;}
    if(!permits(address,bytes,!execute,false,execute)){trap(execute?1:5,address);return false;}
    if(uint64_t(address)+bytes<=ram.size()){value=little(ram.data()+address,bytes);return true;}
    if(execute){trap(1,address);return false;}
    uint32_t word=0;const uint32_t base=address&~3u;
    switch(base){
    case 0x02000000:word=msip?1:0;break;
    case 0x02004000:word=uint32_t(mtimecmp);break;
    case 0x02004004:word=uint32_t(mtimecmp>>32);break;
    case 0x0200bff8:word=uint32_t(mtime);break;
    case 0x0200bffc:word=uint32_t(mtime>>32);break;
    case 0x10000008:word=4;break; // UART TX empty; deterministic immediate sink.
    default:trap(5,address);return false;
    }
    value=word>>((address&3)*8);if(bytes<4)value&=(1u<<(bytes*8))-1;return true;
}
bool Machine::store(uint32_t address,unsigned bytes,uint32_t value) {
    if(address&(bytes-1)){trap(6,address);return false;}
    if(!permits(address,bytes,false,true,false)){trap(7,address);return false;}
    if(uint64_t(address)+bytes<=ram.size()){put_little(ram.data()+address,bytes,value);return true;}
    const uint32_t base=address&~3u,shift=(address&3)*8;
    const uint32_t mask=(bytes==4?UINT32_MAX:(1u<<(bytes*8))-1)<<shift;
    auto merge=[&](uint32_t old){return (old&~mask)|((value<<shift)&mask);};
    switch(base){
    case 0x02000000:msip=(merge(msip?1u:0u)&1)!=0;break;
    case 0x02004000:mtimecmp=(mtimecmp&0xffffffff00000000ull)|merge(uint32_t(mtimecmp));break;
    case 0x02004004:mtimecmp=(mtimecmp&0xffffffffull)|(uint64_t(merge(uint32_t(mtimecmp>>32)))<<32);break;
    case 0x0200bff8:mtime=(mtime&0xffffffff00000000ull)|merge(uint32_t(mtime));break;
    case 0x0200bffc:mtime=(mtime&0xffffffffull)|(uint64_t(merge(uint32_t(mtime>>32)))<<32);break;
    case 0x10000004:
        if((address&3)!=0){trap(7,address);return false;}
        last_uart=uint8_t(value);uart_writes++;
        if(uart_sink)uart_sink(uart_context,last_uart);
        break;
    default:trap(7,address);return false;
    }
    return true;
}
bool Machine::csr_read(uint32_t a,uint32_t& v) const {
    switch(a){
    case 0x300:v=mstatus;break;
    case 0x301:v=0x40001100u|(profile.compressed?4u:0u)|(profile.user_pmp?0x100000u:0u);break;
    case 0x304:v=mie;break;case 0x305:v=mtvec;break;
    case 0x306:if(!profile.user_pmp)return false;v=mcounteren;break;
    case 0x340:v=mscratch;break;case 0x341:v=mepc;break;case 0x342:v=mcause;break;case 0x343:v=mtval;break;case 0x344:v=pending();break;
    case 0xb00:case 0xc00:v=uint32_t(cycles);break;case 0xb80:case 0xc80:v=uint32_t(cycles>>32);break;
    case 0xb02:case 0xc02:v=uint32_t(retired);break;case 0xb82:case 0xc82:v=uint32_t(retired>>32);break;
    case 0xc01:v=uint32_t(mtime);break;case 0xc81:v=uint32_t(mtime>>32);break;
    case 0xf11:case 0xf12:case 0xf13:case 0xf14:v=0;break;
    case 0xfc0:v=profile.zaamo?1:0;break;
    default:
        if(!profile.user_pmp)return false;
        if(a>=0x3a0&&a<=0x3a3){v=a==0x3a0?pmpcfg:0;break;}
        if(a>=0x3b0&&a<=0x3bf){v=a<0x3b4?pmpaddr[a-0x3b0]:0;break;}
        return false;
    }
    return true;
}
void Machine::csr_write(uint32_t a,uint32_t v) {
    switch(a){
    case 0x300:
        if(profile.user_pmp){mstatus=v&0x221888u;if(((v>>11)&3)!=0)mstatus|=0x1800u;}
        else mstatus=0x1800u|(v&0x88u);
        break;
    case 0x304:mie=v&0x888;break;case 0x305:mtvec=v&~3u;break;
    case 0x306:mcounteren=v&7;break;
    case 0x340:mscratch=v;break;case 0x341:mepc=v&~uint32_t(profile.compressed?1:3);break;case 0x342:mcause=v;break;case 0x343:mtval=v;break;
    case 0xb00:cycles=(cycles&0xffffffff00000000ull)|v;break;case 0xb80:cycles=(cycles&0xffffffffull)|(uint64_t(v)<<32);break;
    case 0xb02:retired=(retired&0xffffffff00000000ull)|v;break;case 0xb82:retired=(retired&0xffffffffull)|(uint64_t(v)<<32);break;
    case 0x3a0:
        for(unsigned i=0;i<4;i++)if(!(pmpcfg&(128u<<(i*8)))){
            uint32_t c=(v>>(i*8))&0x9f;if((c&3)==2)c&=~2u;
            pmpcfg=(pmpcfg&~(255u<<(i*8)))|(c<<(i*8));
        }
        break;
    default:
        if(a>=0x3b0&&a<=0x3b3){unsigned i=a-0x3b0;bool locked=(pmpcfg&(128u<<(i*8)))!=0;
            if(i<3){const uint32_t next=pmpcfg>>((i+1)*8);locked=locked||((next&128)&&((next>>3)&3)==1);}
            if(!locked)pmpaddr[i]=v&0x3fffffff;
        }
        break; // Fixed misa, mip and unused PMP entries implement WARL writes.
    }
}

bool Machine::decompress(uint16_t c,uint32_t& out) {
    const unsigned rd=(c>>7)&31,rs2=(c>>2)&31,rp=8+((c>>7)&7),rq=8+((c>>2)&7),f=c>>13;
    const uint32_t ci=sx(((c>>12)&1)<<5|((c>>2)&31),6);
    out=0x13;
    switch(c&3){
    case 0:{
        const uint32_t off=((c>>5)&1)<<6|((c>>10)&7)<<3|((c>>6)&1)<<2;
        if(f==0){const uint32_t v=((c>>7)&15)<<6|((c>>11)&3)<<4|((c>>5)&1)<<3|((c>>6)&1)<<2;out=enc_i(0x13,0,rq,2,v);return v!=0;}
        if(f==2){out=enc_i(3,2,rq,rp,off);return true;}
        if(f==6){out=enc_s(rp,rq,off);return true;}
        return false;
    }
    case 1:
        switch(f){
        case 0:out=enc_i(0x13,0,rd,rd,ci);return true;
        case 1:case 5:{const uint32_t v=sx(((c>>12)&1)<<11|((c>>8)&1)<<10|((c>>9)&3)<<8|((c>>6)&1)<<7|((c>>7)&1)<<6|((c>>2)&1)<<5|((c>>11)&1)<<4|((c>>3)&7)<<1,12);out=enc_j(f==1?1:0,v);return true;}
        case 2:out=enc_i(0x13,0,rd,0,ci);return true;
        case 3:
            if(rd==2){const uint32_t v=sx(((c>>12)&1)<<9|((c>>3)&3)<<7|((c>>5)&1)<<6|((c>>2)&1)<<5|((c>>6)&1)<<4,10);out=enc_i(0x13,0,2,2,v);return v!=0;}
            out=(ci<<12)|(rd<<7)|0x37;return ci!=0;
        case 4:
            switch((c>>10)&3){
            case 0:case 1:out=enc_i(0x13,5,rp,rp,((c>>10)&1?0x400u:0u)|rs2);return !(c&0x1000);
            case 2:out=enc_i(0x13,7,rp,rp,ci);return true;
            default:{const unsigned sub=(c>>5)&3;const unsigned f3[4]={0,4,6,7};out=enc_r(sub==0?0x20:0,f3[sub],rp,rp,rq);return !(c&0x1000);}
            }
        case 6:case 7:{const uint32_t v=sx(((c>>12)&1)<<8|((c>>5)&3)<<6|((c>>2)&1)<<5|((c>>10)&3)<<3|((c>>3)&3)<<1,9);out=enc_b(f==6?0:1,rp,v);return true;}
        default:return false;
        }
    case 2:
        switch(f){
        case 0:out=enc_i(0x13,1,rd,rd,rs2);return !(c&0x1000);
        case 2:{const uint32_t v=((c>>2)&3)<<6|((c>>12)&1)<<5|((c>>4)&7)<<2;out=enc_i(3,2,rd,2,v);return rd!=0;}
        case 4:
            if(!(c&0x1000)){if(!rs2){out=enc_i(0x67,0,0,rd,0);return rd!=0;}out=enc_r(0,0,rd,0,rs2);return true;}
            if(rs2){out=enc_r(0,0,rd,rd,rs2);return true;}
            out=rd?enc_i(0x67,0,1,rd,0):0x00100073;return true;
        case 6:{const uint32_t v=((c>>7)&3)<<6|((c>>9)&15)<<2;out=enc_s(2,rs2,v);return true;}
        default:return false;
        }
    default:return false;
    }
}

void Machine::run(uint64_t count) { while(count--)step(); }
void Machine::step() {
    // Timer progress is deterministic and independent of host scheduling. An
    // interrupt is taken before executing the next guest instruction.
    last_retired=false;last_rd=0;x[0]=0;steps++;
    cycles+=profile.cycles_per_instruction;mtime+=profile.cycles_per_instruction;
    const uint32_t irq=pending()&mie;
    if(irq&&((mstatus&8)||(profile.user_pmp&&user))){trap(irq&2048?0x8000000b:irq&8?0x80000003:0x80000007,0);return;}
    const uint32_t here=pc;last_pc=here;
    if(here&(profile.compressed?1u:3u)){trap(0,here);return;}
    if(!permits(here,2,false,false,true)||uint64_t(here)+2>ram.size()){trap(1,here);return;}
    uint32_t raw=little(ram.data()+here,2),insn=raw;unsigned length=2;
    if((raw&3)==3){length=4;
        if(uint64_t(here)+4>ram.size()||!permits(here,4,false,false,true)){trap(1,here+2);return;}
        raw=little(ram.data()+here,4);insn=raw;
    }else if(!profile.compressed){last_raw=raw;trap(2,uint64_t(here)+4<=ram.size()?little(ram.data()+here,4):raw);return;}
    last_raw=raw;
    auto& cached=cache[(here/2)&(cache.size()-1)];
    if(cached.length==length&&cached.raw==raw)insn=cached.insn;
    else {
        if(length==2&&!decompress(uint16_t(raw),insn)){trap(2,raw);return;}
        cached={raw,insn,uint8_t(length)};
    }
    const unsigned rd=(insn>>7)&31,rs1=(insn>>15)&31,rs2=(insn>>20)&31,f3=(insn>>12)&7,f7=insn>>25;
    const uint32_t a=x[rs1],b=x[rs2],imm=sx(insn>>20,12);
    uint32_t next=here+length,result=0;bool write=false,valid=true;
    auto jump=[&](uint32_t target){if(target&(profile.compressed?1u:3u)){trap(0,target);return false;}next=target;return true;};
    switch(insn&127){
    case 0x37:result=insn&0xfffff000;write=true;break;
    case 0x17:result=here+(insn&0xfffff000);write=true;break;
    case 0x6f:{const uint32_t off=sx((insn>>31)<<20|((insn>>12)&255)<<12|((insn>>20)&1)<<11|((insn>>21)&1023)<<1,21);if(!jump(here+off))return;result=here+length;write=true;break;}
    case 0x67:if(f3){valid=false;break;}if(!jump((a+imm)&~1u))return;result=here+length;write=true;break;
    case 0x63:{const uint32_t off=sx((insn>>31)<<12|((insn>>7)&1)<<11|((insn>>25)&63)<<5|((insn>>8)&15)<<1,13);bool taken=false;
        switch(f3){case 0:taken=a==b;break;case 1:taken=a!=b;break;case 4:taken=si(a)<si(b);break;case 5:taken=si(a)>=si(b);break;case 6:taken=a<b;break;case 7:taken=a>=b;break;default:valid=false;}
        if(taken&&!jump(here+off))return;
        break;}
    case 3:{unsigned bytes=0;switch(f3){case 0:case 4:bytes=1;break;case 1:case 5:bytes=2;break;case 2:bytes=4;break;default:valid=false;}
        if(!valid)break;
        if(!load(a+imm,bytes,result))return;
        if(f3==0)result=sx(result,8);
        if(f3==1)result=sx(result,16);
        write=true;break;}
    case 0x23:{if(f3>2){valid=false;break;}const uint32_t off=sx(((insn>>25)<<5)|((insn>>7)&31),12);if(!store(a+off,1u<<f3,b))return;break;}
    case 0x13:
        write=true;
        switch(f3){
        case 0:result=a+imm;break;case 2:result=si(a)<si(imm);break;case 3:result=a<imm;break;
        case 4:result=a^imm;break;case 6:result=a|imm;break;case 7:result=a&imm;break;
        case 1:
            if(f7==0)result=a<<rs2;
            else if(profile.bitmanip&&f7==0x30){switch(rs2){case 0:result=std::countl_zero(a);break;case 1:result=std::countr_zero(a);break;case 2:result=std::popcount(a);break;case 4:result=sx(a&255,8);break;case 5:result=sx(a&65535,16);break;default:valid=false;}}
            else if(profile.bitmanip&&f7==0x14)result=a|(1u<<rs2);
            else if(profile.bitmanip&&f7==0x24)result=a&~(1u<<rs2);
            else if(profile.bitmanip&&f7==0x34)result=a^(1u<<rs2);else valid=false;
            break;
        case 5:
            if(f7==0)result=a>>rs2;else if(f7==0x20)result=uint32_t(si(a)>>rs2);
            else if(profile.bitmanip&&f7==0x30)result=std::rotr(a,int(rs2));
            else if(profile.bitmanip&&f7==0x24)result=(a>>rs2)&1;
            else if(profile.bitmanip&&(insn>>20)==0x287){for(unsigned i=0;i<4;i++)if((a>>(8*i))&255)result|=255u<<(8*i);}
            else if(profile.bitmanip&&(insn>>20)==0x698)result=((a&255)<<24)|((a&0xff00)<<8)|((a>>8)&0xff00)|(a>>24);else valid=false;
            break;
        default:valid=false;
        }break;
    case 0x33:
        write=true;
        if(f7==1){
            switch(f3){case 0:result=uint32_t(uint64_t(a)*b);break;case 1:result=uint32_t(uint64_t(int64_t(si(a))*si(b))>>32);break;case 2:result=uint32_t(uint64_t(int64_t(si(a))*int64_t(b))>>32);break;case 3:result=uint32_t((uint64_t(a)*b)>>32);break;
            case 4:result=b==0?UINT32_MAX:a==0x80000000&&b==UINT32_MAX?a:uint32_t(si(a)/si(b));break;
            case 5:result=b?a/b:UINT32_MAX;break;
            case 6:result=b==0?a:a==0x80000000&&b==UINT32_MAX?0:uint32_t(si(a)%si(b));break;
            case 7:result=b?a%b:a;break;}
        }else if(f7==0){switch(f3){case 0:result=a+b;break;case 1:result=a<<(b&31);break;case 2:result=si(a)<si(b);break;case 3:result=a<b;break;case 4:result=a^b;break;case 5:result=a>>(b&31);break;case 6:result=a|b;break;case 7:result=a&b;break;}}
        else if(f7==0x20){switch(f3){case 0:result=a-b;break;case 5:result=uint32_t(si(a)>>(b&31));break;case 4:result=~(a^b);valid=profile.bitmanip;break;case 6:result=a|~b;valid=profile.bitmanip;break;case 7:result=a&~b;valid=profile.bitmanip;break;default:valid=false;}}
        else if(profile.bitmanip){
            if(f7==0x30&&(f3==1||f3==5))result=f3==1?std::rotl(a,int(b&31)):std::rotr(a,int(b&31));
            else if(f7==4&&f3==4&&rs2==0)result=a&65535;
            else if(f7==5){switch(f3){case 1:result=uint32_t(clmul(a,b));break;case 2:result=uint32_t(clmul(a,b)>>31);break;case 3:result=uint32_t(clmul(a,b)>>32);break;case 4:result=si(a)<si(b)?a:b;break;case 5:result=std::min(a,b);break;case 6:result=si(a)>si(b)?a:b;break;case 7:result=std::max(a,b);break;default:valid=false;}}
            else if(f7==0x14&&f3==1)result=a|(1u<<(b&31));
            else if(f7==0x24&&f3==1)result=a&~(1u<<(b&31));
            else if(f7==0x34&&f3==1)result=a^(1u<<(b&31));
            else if(f7==0x24&&f3==5)result=(a>>(b&31))&1;else valid=false;
        }else valid=false;
        break;
    case 0x0f:valid=f3==0||f3==1;break; // Coherent RAM and raw-validated decode cache.
    case 0x2f:{
        const unsigned op=insn>>27;
        if(!profile.zaamo||f3!=2||!(op==0||op==1||op==4||op==8||op==12||op==16||op==20||op==24||op==28)){valid=false;break;}
        if(a&3){trap(6,a);return;}
        if(!permits(a,4,true,true,false)||uint64_t(a)+4>ram.size()){trap(7,a);return;}
        const uint32_t old=little(ram.data()+a,4);uint32_t value=0;
        switch(op){case 0:value=old+b;break;case 1:value=b;break;case 4:value=old^b;break;case 8:value=old|b;break;case 12:value=old&b;break;case 16:value=si(old)<si(b)?old:b;break;case 20:value=si(old)>si(b)?old:b;break;case 24:value=std::min(old,b);break;case 28:value=std::max(old,b);break;}
        put_little(ram.data()+a,4,value);result=old;write=true;amo_operations++;break;
    }
    case 0x73:
        if(f3==0){
            if(insn==0x00000073){trap(user?8:11,0);return;}
            if(insn==0x00100073){trap(3,here);return;}
            if(insn==0x30200073){
                if(user){valid=false;break;}
                const bool return_user=profile.user_pmp&&((mstatus>>11)&3)==0;
                const uint32_t interrupt=(mstatus&128)?8:0;
                mstatus=(mstatus&~0x1888u)|interrupt|128|(profile.user_pmp?0:0x1800);
                if(return_user)mstatus&=~(1u<<17);
                user=return_user;next=mepc;
            }else if(insn==0x10500073){
                if(user&&(mstatus&(1u<<21))){valid=false;break;}
                // Pending enabled sources already wake WFI, even with MIE off.
                if(profile.wfi_sleep&&!(pending()&mie)&&(mie&128)&&mtimecmp>mtime&&mtimecmp!=UINT64_MAX){
                    const uint64_t skip=mtimecmp-mtime;mtime+=skip;cycles+=skip;skipped_cycles+=skip;
                }
            }else valid=false;
        }else if((f3&3)!=0){
            const unsigned address=insn>>20,operation=f3&3;const uint32_t operand=(f3&4)?rs1:a;
            const bool intent=operation==1||rs1!=0;
            uint32_t old=0;
            bool allowed=!user||((address>>8)&3)==0;
            if(user&&(address==0xc00||address==0xc80)&&!(mcounteren&1))allowed=false;
            if(user&&(address==0xc01||address==0xc81)&&!(mcounteren&2))allowed=false;
            if(user&&(address==0xc02||address==0xc82)&&!(mcounteren&4))allowed=false;
            if(!allowed||!csr_read(address,old)||(intent&&(address>>10)==3)){valid=false;break;}
            if(intent)csr_write(address,operation==1?operand:operation==2?old|operand:old&~operand);
            result=old;write=true;
        }else valid=false;
        break;
    default:valid=false;
    }
    if(!valid){trap(2,raw);return;}
    if(write&&rd){x[rd]=result;last_rd=rd;last_result=result;}
    // Explicit minstret writes replace this instruction's implicit increment.
    // CSRRS/CSRRC with rs1=x0 are reads and must still retire normally.
    const bool counter_write=(insn&0x7f)==0x73&&(f3&3)!=0&&
        ((insn>>20)==0xb02||(insn>>20)==0xb82)&&((f3&3)==1||rs1!=0);
    pc=next;if(!counter_write)retired++;last_retired=true;
}
}
