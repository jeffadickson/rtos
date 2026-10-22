#include "elf.hpp"
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <algorithm>
#include <limits>

namespace nexrt::rv {
Image::Image(const std::filesystem::path& path,Machine& machine) {
    if(std::filesystem::file_size(path)>64u*1024u*1024u)throw std::runtime_error("ELF exceeds 64 MiB input limit");
    std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Cannot open ELF: "+path.string());
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)),{});
    auto range=[&](uint64_t offset,uint64_t size){if(offset>data.size()||size>data.size()-offset)throw std::runtime_error("Truncated ELF range");};
    auto u16=[&](uint64_t offset){range(offset,2);return uint32_t(data[size_t(offset)])|(uint32_t(data[size_t(offset+1)])<<8);};
    auto u32=[&](uint64_t offset){range(offset,4);uint32_t v=0;for(unsigned b=0;b<4;b++)v|=uint32_t(data[size_t(offset+b)])<<(8*b);return v;};
    range(0,52);
    if(data[0]!=0x7f||data[1]!='E'||data[2]!='L'||data[3]!='F'||data[4]!=1||data[5]!=1||data[6]!=1||u16(16)!=2||u16(18)!=243||u32(20)!=1||u16(40)!=52)
        throw std::runtime_error("Expected an ELF32 little-endian RISC-V executable");
    if(u32(36)&6)throw std::runtime_error("Floating-point ELF ABI is unsupported");
    if(u32(36)&8)throw std::runtime_error("RV32E ELF ABI is unsupported");
    entry=u32(24);const uint32_t ph=u32(28),sh=u32(32),phsize=u16(42),phnum=u16(44),shsize=u16(46),shnum=u16(48);
    if(phsize!=32||!phnum)throw std::runtime_error("Unsupported ELF program-header layout");
    range(ph,uint64_t(phnum)*phsize);
    std::vector<std::pair<uint64_t,uint64_t>> intervals;
    bool executable_entry=false;
    for(uint32_t i=0;i<phnum;i++){
        const uint64_t h=uint64_t(ph)+uint64_t(i)*phsize;if(u32(h)!=1)continue;
        const uint32_t offset=u32(h+4),address=u32(h+8),physical=u32(h+12),files=u32(h+16),memory=u32(h+20);
        const uint32_t flags=u32(h+24),alignment=u32(h+28);
        // ELF permits 0/1 for no alignment constraint. Larger values must be
        // powers of two and the file/virtual offsets must be congruent.
        if(alignment>1&&((alignment&(alignment-1))||((address-offset)&(alignment-1))))
            throw std::runtime_error("Invalid ELF load-segment alignment");
        if(files>memory||uint64_t(address)+memory>machine.ram.size()||physical!=address)throw std::runtime_error("ELF segment does not fit the flat target RAM");
        range(offset,files);
        // Reject ambiguous overlap instead of silently changing loader order.
        if(memory){for(auto [lo,hi]:intervals)if(address<hi&&uint64_t(address)+memory>lo)throw std::runtime_error("Overlapping ELF load segments");intervals.emplace_back(address,uint64_t(address)+memory);}
        if((flags&1)&&entry>=address&&uint64_t(entry)<uint64_t(address)+memory)executable_entry=true;
        std::fill_n(machine.ram.data()+address,memory,0);std::copy_n(data.data()+offset,files,machine.ram.data()+address);
    }
    if(!executable_entry||entry>=machine.ram.size()||(entry&(machine.profile.compressed?1u:3u)))throw std::runtime_error("Invalid ELF executable entry point");
    if(shnum){
        if(shsize!=40)throw std::runtime_error("Unsupported ELF section layout");
        range(sh,uint64_t(shnum)*shsize);
        for(uint32_t i=0;i<shnum;i++){
            const uint64_t section=uint64_t(sh)+uint64_t(i)*shsize;
            if(u32(section+4)!=2)continue;
            const uint32_t offset=u32(section+16),size=u32(section+20),link=u32(section+24),stride=u32(section+36);
            if(stride!=16||size%stride||link>=shnum)throw std::runtime_error("Invalid ELF symbol table");
            range(offset,size);
            const uint64_t strings=uint64_t(sh)+uint64_t(link)*shsize;
            if(u32(strings+4)!=3)throw std::runtime_error("ELF symbol names require a string-table section");
            const uint32_t names=u32(strings+16),nameSize=u32(strings+20);range(names,nameSize);
            for(uint32_t k=0;k<size;k+=stride){
                const uint64_t symbol=uint64_t(offset)+k;const uint32_t name=u32(symbol);
                if(name>=nameSize)throw std::runtime_error("ELF symbol name outside string table");
                auto begin=data.begin()+names+name,end=data.begin()+names+nameSize;
                auto zero=std::find(begin,end,uint8_t(0));if(zero==end)throw std::runtime_error("Unterminated ELF symbol name");
                // Diagnostic lookup is an external-name contract. Local symbols
                // may legally share a spelling across translation units and must
                // never hide the firmware's global status/clock definitions.
                const unsigned binding=data[size_t(symbol+12)]>>4;
                if(begin!=zero&&u16(symbol+14)!=0&&(binding==1||binding==2)){
                    const Symbol definition{u32(symbol+4),u32(symbol+8)};
                    const auto [found,inserted]=symbols.emplace(std::string(begin,zero),definition);
                    if(!inserted&&(found->second.address!=definition.address||found->second.size!=definition.size))
                        throw std::runtime_error("Conflicting ELF global diagnostic symbol");
                }
            }
        }
    }
    machine.pc=entry;
}
}
