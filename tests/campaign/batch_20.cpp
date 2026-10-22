#include "common.hpp"
#include "elf_fixture.hpp"
using namespace campaign_elf;
struct Symbols:Fixture {
    Symbols(){
        data.resize(512);w32(32,144);w16(46,40);w16(48,3);
        w32(188,2);w32(200,320);w32(204,32);w32(208,2);w32(220,16);
        w32(228,3);w32(240,400);w32(244,8);
        const char names[8]={'\0','s','t','a','t','u','s','\0'};
        for(unsigned i=0;i<8;i++)data[400+i]=uint8_t(names[i]);
        symbol(336,4,1);
    }
    void symbol(size_t offset,uint32_t address,uint8_t binding){w32(offset,1);w32(offset+4,address);w32(offset+8,4);data[offset+12]=uint8_t(binding<<4);w16(offset+14,0xfff1);}
};
CASE(191,"NXR-091","global diagnostic symbol has checked address and size"){Symbols f;f.save();nexrt::rv::Machine m;nexrt::rv::Image image(f.file,m);const auto s=image.symbols.at("status");assert(s.address==4&&s.size==4);}
CASE(192,"NXR-091","symbol name table must actually be STRTAB"){Symbols f;f.w32(228,1);assert(!f.accepts());}
CASE(193,"NXR-091","symbol section must contain complete entries"){Symbols f;f.w32(204,31);assert(!f.accepts());}
CASE(194,"NXR-091","symbol name must terminate inside string table"){Symbols f;f.data[407]='x';assert(!f.accepts());}
CASE(195,"NXR-091","local duplicate cannot hide global diagnostic identity"){Symbols f;f.symbol(336,8,0);f.w32(204,48);f.symbol(352,4,1);f.save();nexrt::rv::Machine m;nexrt::rv::Image image(f.file,m);assert(image.symbols.at("status").address==4);}
CASE(196,"NXR-091","conflicting global diagnostic definitions rejected"){Symbols f;f.w32(204,48);f.symbol(352,8,1);assert(!f.accepts());}
CASE(197,"NXR-091","identical repeated global symbol accepted"){Symbols f;f.w32(204,48);f.symbol(352,4,1);assert(f.accepts());}
CASE(198,"NXR-091","undefined diagnostic symbol not exported"){Symbols f;f.w16(350,0);f.save();nexrt::rv::Machine m;nexrt::rv::Image image(f.file,m);assert(!image.symbols.contains("status"));}
CASE(199,"NXR-091","symbol string-table link out of range rejected"){Symbols f;f.w32(208,3);assert(!f.accepts());}
CASE(200,"NXR-091","symbol name offset outside string table rejected"){Symbols f;f.w32(336,8);assert(!f.accepts());}
