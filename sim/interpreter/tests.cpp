#include "machine.hpp"
#include "elf.hpp"
#include <algorithm>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace nexrt::rv;
namespace {
uint64_t checks=0;
void need(bool ok,const std::string& why){checks++;if(!ok)throw std::runtime_error(why);}
uint32_t r(unsigned f7,unsigned f3,unsigned rd=3,unsigned a=1,unsigned b=2){return (f7<<25)|(b<<20)|(a<<15)|(f3<<12)|(rd<<7)|0x33;}
uint32_t csr(unsigned address,unsigned f3=1,unsigned rd=0,unsigned source=1){return (address<<20)|(source<<15)|(f3<<12)|(rd<<7)|0x73;}
void instruction(Machine& m,uint32_t word){m.pc=0;m.poke32(0,word);m.step();}
void execute(Machine& m,uint32_t word,uint32_t a,uint32_t b,uint32_t expected){m.x[1]=a;m.x[2]=b;const auto traps=m.last_trap.sequence;instruction(m,word);need(m.last_retired&&m.last_trap.sequence==traps&&m.x[3]==expected,"integer result word="+std::to_string(word));}
void arithmetic(){
    Machine m;std::mt19937 rng(0x4e585254);
    for(unsigned i=0;i<3000;i++){
        const uint32_t a=uint32_t(rng()),b=uint32_t(rng());const int32_t sa=std::bit_cast<int32_t>(a),sb=std::bit_cast<int32_t>(b);
        execute(m,r(0,0),a,b,a+b);execute(m,r(0x20,0),a,b,a-b);execute(m,r(0,4),a,b,a^b);execute(m,r(0,6),a,b,a|b);execute(m,r(0,7),a,b,a&b);
        execute(m,r(0,1),a,b,a<<(b%32));execute(m,r(0,5),a,b,a>>(b%32));execute(m,r(0x20,5),a,b,uint32_t(sa>>(b%32)));
        execute(m,r(0,2),a,b,sa<sb);execute(m,r(0,3),a,b,a<b);
        execute(m,r(1,0),a,b,uint32_t(uint64_t(a)*b));execute(m,r(1,1),a,b,uint32_t(uint64_t(int64_t(sa)*sb)>>32));execute(m,r(1,2),a,b,uint32_t(uint64_t(int64_t(sa)*int64_t(b))>>32));execute(m,r(1,3),a,b,uint32_t((uint64_t(a)*b)>>32));
        if(b){execute(m,r(1,5),a,b,a/b);execute(m,r(1,7),a,b,a%b);if(!(a==0x80000000&&b==UINT32_MAX)){execute(m,r(1,4),a,b,uint32_t(sa/sb));execute(m,r(1,6),a,b,uint32_t(sa%sb));}}
        execute(m,r(0x30,1),a,b,std::rotl(a,int(b%32)));execute(m,r(0x30,5),a,b,std::rotr(a,int(b%32)));
        execute(m,r(5,4),a,b,sa<sb?a:b);execute(m,r(5,5),a,b,std::min(a,b));execute(m,r(5,6),a,b,sa>sb?a:b);execute(m,r(5,7),a,b,std::max(a,b));
        execute(m,r(0x20,4),a,b,~(a^b));execute(m,r(0x20,6),a,b,a|~b);execute(m,r(0x20,7),a,b,a&~b);
    }
    execute(m,r(1,4),0x80000000,UINT32_MAX,0x80000000);execute(m,r(1,6),0x80000000,UINT32_MAX,0);
    execute(m,r(1,4),9,0,UINT32_MAX);execute(m,r(1,5),9,0,UINT32_MAX);execute(m,r(1,6),9,0,9);execute(m,r(1,7),9,0,9);
    execute(m,0x60009193,0,0,32);execute(m,0x60109193,0,0,32);execute(m,0x60209193,0xffffffff,0,32);
    execute(m,0x60409193,0x80,0,0xffffff80);execute(m,0x60509193,0x8000,0,0xffff8000);
    execute(m,0x2870d193,0x01008000,0,0xff00ff00);execute(m,0x6980d193,0x12345678,0,0x78563412);
    // Polynomial multiply examples from an independent bit-position oracle.
    for(unsigned i=0;i<32;i++)for(unsigned j=0;j<32;j++){
        const uint64_t polynomial=uint64_t(1)<<(i+j);execute(m,r(5,1),1u<<i,1u<<j,uint32_t(polynomial));execute(m,r(5,2),1u<<i,1u<<j,uint32_t(polynomial>>31));execute(m,r(5,3),1u<<i,1u<<j,uint32_t(polynomial>>32));
    }
    m.x[0]=123;instruction(m,0x00100013);need(m.x[0]==0,"x0 writes ignored");
    instruction(m,0x00100193);need(m.x[3]==1,"cache first");instruction(m,0x00200193);need(m.x[3]==2,"self modifying cached instruction");
}
void memory(){
    Machine m;m.x[1]=0x200;m.x[2]=0x12345680;
    instruction(m,0x00208023);need(m.ram[0x200]==0x80,"SB");instruction(m,0x00008183);need(m.x[3]==0xffffff80,"LB");instruction(m,0x0000c183);need(m.x[3]==0x80,"LBU");
    instruction(m,0x00209023);instruction(m,0x00009183);need(m.x[3]==0x5680,"LH");m.x[2]=0x81234567;instruction(m,0x0020a023);instruction(m,0x0000a183);need(m.x[3]==m.x[2],"SW LW");
    m.x[1]=0x201;instruction(m,0x0000a183);need(m.mcause==4&&m.mtval==0x201,"load alignment");instruction(m,0x0020a023);need(m.mcause==6&&m.mtval==0x201,"store alignment");
    m.x[1]=0x80000000;instruction(m,0x0000a183);need(m.mcause==5,"unmapped read");instruction(m,0x0020a023);need(m.mcause==7,"unmapped write");
    m.x[1]=0x02004000;m.x[2]=0xfffffff0;instruction(m,0x0020a023);need(uint32_t(m.mtimecmp)==0xfffffff0,"CLINT compare low");
    m.x[1]=0x02004004;m.x[2]=3;instruction(m,0x0020a023);need(m.mtimecmp==0x3fffffff0ull,"CLINT compare high");
}
void atomics(){
    Profile p;p.zaamo=true;Machine m(p);std::mt19937 rng(42);
    for(unsigned op:{0u,1u,4u,8u,12u,16u,20u,24u,28u})for(unsigned order=0;order<4;order++)for(unsigned i=0;i<64;i++){
        uint32_t old=uint32_t(rng()),value=uint32_t(rng()),expected=0;
        switch(op){case 0:expected=old+value;break;case 1:expected=value;break;case 4:expected=old^value;break;case 8:expected=old|value;break;case 12:expected=old&value;break;case 16:expected=std::bit_cast<int32_t>(old)<std::bit_cast<int32_t>(value)?old:value;break;case 20:expected=std::bit_cast<int32_t>(old)>std::bit_cast<int32_t>(value)?old:value;break;case 24:expected=std::min(old,value);break;case 28:expected=std::max(old,value);break;}
        m.x[1]=0x200;m.x[2]=value;m.poke32(0x200,old);instruction(m,(op<<27)|(order<<25)|(2<<20)|(1<<15)|(2<<12)|(3<<7)|0x2f);
        need(m.last_retired&&m.x[3]==old&&m.peek32(0x200)==expected,"AMO operation/order");
    }
    m.x[1]=0x201;instruction(m,0x0020a1af);need(m.mcause==6&&m.mtval==0x201,"AMO alignment");
    instruction(m,0x1000a1af);need(m.mcause==2,"Zaamo does not implement LR");instruction(m,0x1820a1af);need(m.mcause==2,"Zaamo does not implement SC");
    Machine disabled;disabled.x[1]=0x200;instruction(disabled,0x0020a1af);need(disabled.mcause==2,"AMO default off");
    m.x[1]=0x200;m.x[2]=17;m.poke32(0x200,9);instruction(m,(1u<<27)|(2<<20)|(1<<15)|(2<<12)|(1<<7)|0x2f);need(m.x[1]==9&&m.peek32(0x200)==17,"AMO rd=rs1 captures original address");
}
void privilege(){
    Profile p;p.user_pmp=true;p.zaamo=true;Machine m(p);m.mtvec=0x100;
    m.pmpcfg=0x1f;m.pmpaddr[0]=127; // 1 KiB RWX for the directed user program.
    m.user=true;m.mstatus=8;m.poke32(0,0x73);m.step();need(m.mcause==8&&!m.user&&m.mepc==0&&!(m.mstatus&8)&&(m.mstatus&128)&&!(m.mstatus&0x1800),"U ECALL precise entry");
    m.mepc=4;m.mstatus|=1u<<17;m.poke32(0x100,0x30200073);m.step();need(m.user&&m.pc==4&&(m.mstatus&8)&&(m.mstatus&128)&&!(m.mstatus&(1u<<17))&&!(m.mstatus&0x1800),"MRET to U clears MPRV");
    instruction(m,0x30200073);need(m.mcause==2&&m.mtval==0x30200073,"U MRET forbidden");
    m.user=true;instruction(m,csr(0x300,2,3,0));need(m.mcause==2,"U machine CSR forbidden");
    m.user=true;instruction(m,csr(0xc00,2,3,0));need(m.mcause==2,"U counter gating");
    m.user=true;m.mcounteren=1;const auto sequence=m.last_trap.sequence;instruction(m,csr(0xc00,2,3,0));need(m.last_retired&&m.last_trap.sequence==sequence,"U enabled counter");
    m.user=true;m.mstatus|=1u<<21;instruction(m,0x10500073);need(m.mcause==2,"TW U WFI");
    m.user=true;m.mstatus=0;m.mie=128;m.mtimecmp=0;m.step();need(!m.user&&m.mcause==0x80000007&&!(m.mstatus&0x1800),"U interrupt ignores global MIE");
    Machine timer;timer.mtvec=0x100;timer.mie=128;timer.mstatus|=8;timer.mtimecmp=4;timer.poke32(0,0x13);timer.poke32(4,0x13);timer.poke32(8,0x13);
    timer.run(3);need(timer.pc==12&&!timer.timer_interrupts,"timer not early");timer.step();need(timer.mepc==12&&timer.timer_interrupts==1&&!(timer.mstatus&8)&&(timer.mstatus&128),"timer instruction boundary");
    timer.poke32(0x100,0x30200073);timer.mtimecmp=UINT64_MAX;timer.step();need(timer.pc==12&&(timer.mstatus&8),"timer return resumes interrupted PC");
    Machine masked;masked.mie=128;masked.mtimecmp=0;masked.poke32(0,0x13);masked.step();need(!masked.timer_interrupts,"masked timer remains pending");masked.mstatus|=8;masked.step();need(masked.timer_interrupts==1,"pending timer on enable");
    Profile sleep;sleep.wfi_sleep=true;Machine idle(sleep);idle.mie=128;idle.mstatus|=8;idle.mtimecmp=10000;idle.poke32(0,0x10500073);idle.step();need(idle.pc==4&&idle.mtime==10000&&idle.skipped_cycles==9999,"WFI explicit fast-forward");idle.step();need(idle.timer_interrupts==1&&idle.mepc==4,"WFI pending interrupt");
    Machine nop;nop.mtimecmp=10000;nop.poke32(0,0x10500073);nop.step();need(nop.mtime==1&&!nop.skipped_cycles,"default WFI preserves NOP");
}
void pmp(){
    Profile p;p.user_pmp=true;p.zaamo=true;Machine m(p);
    m.user=true;need(!m.permits(0x200,4,true,false,false),"default deny U");m.user=false;need(m.permits(0x200,4,true,true,false),"unmatched M bypass");
    m.pmpcfg=0x19;m.pmpaddr[0]=0x9f; // NAPOT 0x200..0x300, read only.
    m.user=true;need(m.permits(0x200,4,true,false,false)&&m.permits(0x2fc,4,true,false,false),"NAPOT boundaries");need(!m.permits(0x300,1,true,false,false)&&!m.permits(0x200,4,false,true,false),"NAPOT denial");
    m.user=false;need(m.permits(0x200,4,false,true,false),"M unlocked bypass");m.pmpcfg|=128;need(!m.permits(0x200,4,false,true,false),"locked applies M");
    m.pmpcfg=0x1515;m.pmpaddr[0]=0x80;m.pmpaddr[1]=0x81;need(!m.permits(0x202,4,true,false,true),"one entry must cover whole instruction");
    m.user=true;m.pmpcfg=0x1f00;m.pmpaddr[0]=0;m.pmpaddr[1]=0x3fffffff;need(m.permits(0xfffffffc,4,true,true,false)&&!m.permits(0xfffffffe,4,true,true,false),"PMP wrap boundary");
    m.user=false;m.pmpcfg=0x8800;m.pmpaddr={0x100,0x200,0,0};m.x[1]=7;instruction(m,csr(0x3b0));need(m.pmpaddr[0]==0x100,"locked TOR predecessor");instruction(m,csr(0x3b1));need(m.pmpaddr[1]==0x200,"locked own address");m.x[1]=0;instruction(m,csr(0x3a0));need(m.pmpcfg==0x8800,"locked cfg survives writes");
    m.pmpcfg=0;m.x[1]=0xffffffff;instruction(m,csr(0x3b3));need(m.pmpaddr[3]==0x3fffffff,"address WARL");
    m.x[1]=0x22;instruction(m,csr(0x3a0));need(m.pmpcfg==0,"reserved W without R normalization");
    for(unsigned i=4;i<16;i++){m.x[1]=UINT32_MAX;instruction(m,csr(0x3b0+i));instruction(m,csr(0x3b0+i,2,3,0));need(m.x[3]==0,"unused PMP entry hardwired OFF");}
    // MPRV applies only to data; code fetch at zero still uses M privilege.
    m.pmpcfg=0x19;m.pmpaddr[0]=0x9f;m.mstatus=0x20000;m.x[1]=0x200;m.x[2]=0xdeadbeef;m.poke32(0x200,0x12345678);
    instruction(m,0x0020a023);need(m.mcause==7&&m.mtval==0x200&&m.peek32(0x200)==0x12345678,"denied MPRV store has no RAM effect");
    m.mstatus=0x20000; // The preceding trap saved M in MPP; reselect U for this access.
    instruction(m,0x0020a1af);need(m.mcause==7&&m.peek32(0x200)==0x12345678,"AMO needs R and W");
    m.pmpcfg=0;m.mstatus=0;Machine fresh(p);need(!fresh.pmpcfg&&std::all_of(fresh.pmpaddr.begin(),fresh.pmpaddr.end(),[](auto a){return a==0;}),"reset clears locks");
}
void uart_observer(){
    Profile p;p.user_pmp=true;Machine m(p);unsigned observed=0;
    m.uart_context=&observed;
    m.uart_sink=[](void* context,uint8_t value){*static_cast<unsigned*>(context)=value;};
    m.x[1]=0x10000004;m.x[2]='Q';instruction(m,0x0020a023);
    need(m.last_retired&&m.uart_writes==1&&observed=='Q',"UART observer sees committed TX");
    m.x[1]++;m.x[2]='X';instruction(m,0x0020a023);
    need(m.mcause==6&&m.uart_writes==1&&observed=='Q',"misaligned UART store produces no byte");
    m.x[1]=0x10000004;m.pmpcfg=0x98;m.pmpaddr[0]=0x04000000;
    instruction(m,0x0020a023);
    need(m.mcause==7&&m.uart_writes==1&&observed=='Q',"PMP-denied UART store produces no byte");
}
void counter_writes(){
    Machine m;m.retired=0x100000000ull;m.x[1]=42;
    instruction(m,csr(0xb02));need(m.retired==0x10000002aull,"minstret write overrides increment");
    instruction(m,csr(0xc02,2,3,0));need(m.x[3]==42&&m.retired==0x10000002bull,"instret reads pre-retirement value");
    m.x[1]=7;instruction(m,csr(0xb82));need(m.retired==0x70000002bull,"minstreth write overrides increment");
}
/// Reuse the independent RTL fixture ELFs without rebuilding or changing RTL.
/// Each fixture checks its own cause, trap value and saved privilege in firmware.
void rtl_fixtures(const std::filesystem::path& directory){
    unsigned count=0;
    for(const auto& file:std::filesystem::directory_iterator(directory)){
        const auto name=file.path().stem().string();
        const bool c1=name.ends_with("-c1"),c0=name.ends_with("-c0");
        if(file.path().extension()!=".elf"||(!c1&&!c0))continue;
        if((c1&&name.starts_with("compressed-disabled"))||(c0&&(name.starts_with("compressed-permitted")||name.starts_with("straddling-denied"))))continue;
        Profile p;p.compressed=c1;p.user_pmp=true;p.zaamo=true;
        Machine m(p);Image image(file.path(),m);
        while(m.steps<100000&&!m.peek32(0xf00)){
            m.step();
            if(name.starts_with("u-interrupt")&&m.last_retired&&m.last_pc==0x400)m.external_irq=true;
        }
        need(m.peek32(0xf00)==1,"RTL fixture signature: "+name);
        need(m.peek32(0xe00)==0x12345678,"RTL fixture guard: "+name);
        need(m.last_trap.sequence==(name.starts_with("return-to-user")?2u:1u),"RTL fixture trap count: "+name);
        count++;
    }
    need(count==71,"expected 71 compressed/uncompressed RTL fixtures");
    std::cout<<"PASS "<<count<<" independent RTL firmware fixtures\n";
}
void compressed(){
    uint32_t out=0;need(Machine::decompress(0x0001,out)&&out==0x13,"C.NOP");need(Machine::decompress(0x9002,out)&&out==0x00100073,"C.EBREAK");need(!Machine::decompress(0,out),"C.ADDI4SPN zero reserved");need(!Machine::decompress(0x4002,out),"C.LWSP x0 reserved");
    for(unsigned c=0;c<65536;c++)if(Machine::decompress(uint16_t(c),out))need((out&3)==3,"expanded opcode width");
    Machine m;m.poke32(0,0x90020001);m.step();need(m.pc==2&&m.last_retired,"two-byte PC advance");m.step();need(m.mcause==3&&m.mtval==2&&m.mepc==2,"compressed breakpoint PC");
    Profile p;p.compressed=false;Machine no(p);no.poke32(0,0x90020001);no.step();need(no.mcause==2,"compressed disabled");no.pc=2;no.step();need(no.mcause==0&&no.mtval==2,"IALIGN32");
}
void elf(){
    std::vector<uint8_t> bytes(88);auto w16=[&](size_t o,uint32_t v){bytes[o]=uint8_t(v);bytes[o+1]=uint8_t(v>>8);};auto w32=[&](size_t o,uint32_t v){for(unsigned i=0;i<4;i++)bytes[o+i]=uint8_t(v>>(8*i));};
    bytes[0]=0x7f;bytes[1]='E';bytes[2]='L';bytes[3]='F';bytes[4]=1;bytes[5]=1;bytes[6]=1;w16(16,2);w16(18,243);w32(20,1);w32(28,52);w16(40,52);w16(42,32);w16(44,1);w32(52,1);w32(56,84);w32(68,4);w32(72,16);w32(84,0x00700193);
    const auto file=std::filesystem::current_path()/"rv-test-fixture.elf";auto save=[&](){std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));};save();
    Machine m;std::fill(m.ram.begin(),m.ram.end(),0xa5);Image good(file,m);m.step();need(m.x[3]==7&&m.peek32(4)==0&&m.ram[16]==0xa5,"ELF PT_LOAD and BSS");
    bytes[5]=2;save();bool rejected=false;try{Image bad(file,m);}catch(const std::runtime_error&){rejected=true;}need(rejected,"ELF endian rejection");bytes[5]=1;w32(72,0xffffffff);save();rejected=false;try{Image bad(file,m);}catch(const std::runtime_error&){rejected=true;}need(rejected,"ELF segment overflow rejection");std::filesystem::remove(file);
}
}
/// Independent directed and seeded architectural checks; guest traps are tested
/// as architectural events, not mistaken for host execution failures.
int main(int argc,char** argv){
    try{
        if(argc==2&&std::string(argv[1])=="--rvc-table"){for(unsigned c=0;c<65536;c++){uint32_t out=0;bool ok=Machine::decompress(uint16_t(c),out);std::cout<<std::hex<<c<<' '<<out<<' '<<!ok<<'\n';}return 0;}
        if(argc==3&&std::string(argv[1])=="--rtl-fixtures"){rtl_fixtures(argv[2]);return 0;}
        arithmetic();memory();atomics();privilege();pmp();uart_observer();counter_writes();compressed();elf();std::cout<<"PASS RV interpreter "<<checks<<" architectural checks\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<" after "<<checks<<" checks\n";return 1;}
}
