#pragma once
#include "sync_model.hpp"
#include <cstdio>
#include <cstring>
#include <limits.h>
#include <cstdlib>
namespace campaign {
struct Case {unsigned id;const char* requirement;const char* title;void (*run)();};
inline Case cases[10]{};inline unsigned count{};
struct Register {
    Register(unsigned id,const char* requirement,const char* title,void (*run)()){
        assert(count<10);cases[count++]={id,requirement,title,run};
    }
};
}
#define CASE(id,requirement,title) static void case_##id(); static campaign::Register reg_##id(id,requirement,title,case_##id); static void case_##id()
int main(int argc,char** argv){
    assert(campaign::count==10);
    if(argc==2&&std::strcmp(argv[1],"--list")==0){
        for(const auto& c:campaign::cases)std::printf("%u\t%s\t%s\n",c.id,c.requirement,c.title);
        return 0;
    }
    const unsigned selected=argc==3&&std::strcmp(argv[1],"--case")==0?unsigned(std::strtoul(argv[2],nullptr,10)):0;
    bool ran=false;
    for(const auto& c:campaign::cases){
        if(selected&&c.id!=selected)continue;
        ran=true;
        assert(model::depth==0);model::scheduler={};model::isr=false;model::started=true;model::on_park=nullptr;
        rtos::Thread root;model::use(root);assert(model::scheduler.add(root));
        std::printf("RUN %u %s %s\n",c.id,c.requirement,c.title);std::fflush(stdout);
        c.run();assert(model::depth==0);
    }
    if(!ran)return 2;
    std::printf("PASS ten authored acceptance scenarios\n");
}
