#pragma once
#include "elf.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>
namespace campaign_elf {
struct Fixture {
    std::vector<uint8_t> data=std::vector<uint8_t>(256);
    std::filesystem::path directory,file;
    void w16(size_t n,uint32_t v){data[n]=uint8_t(v);data[n+1]=uint8_t(v>>8);}
    void w32(size_t n,uint32_t v){for(unsigned i=0;i<4;i++)data[n+i]=uint8_t(v>>(8*i));}
    Fixture(){
        directory=std::filesystem::temp_directory_path()/("nexrt-campaign-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if(!std::filesystem::create_directory(directory))throw std::runtime_error("Fixture directory collision");
        file=directory/"input.elf";
        data[0]=0x7f;data[1]='E';data[2]='L';data[3]='F';data[4]=1;data[5]=1;data[6]=1;
        w16(16,2);w16(18,243);w32(20,1);w32(28,52);w16(40,52);w16(42,32);w16(44,1);
        w32(52,1);w32(56,128);w32(68,4);w32(72,16);w32(76,5);w32(80,4);w32(128,0x00700193);
    }
    ~Fixture(){std::filesystem::remove(file);std::filesystem::remove(directory);}
    void save(){std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));if(!out)throw std::runtime_error("Cannot save fixture");}
    bool accepts(nexrt::rv::Profile profile={}){
        save();nexrt::rv::Machine m(profile);
        try{nexrt::rv::Image image(file,m);return true;}catch(const std::runtime_error&){return false;}
    }
};
}
