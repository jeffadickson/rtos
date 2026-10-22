#include "common.hpp"
#include "rtos_fault_record.hpp"
using namespace rtos;
CASE(21,"NXR-073","empty fault leaves caller output unchanged"){FirstFault f;FaultRecord r{77};assert(!f.snapshot(r)&&r.cause==77);}
CASE(22,"NXR-073","first fault accepted"){FirstFault f;assert(f.capture({7,100,200}));FaultRecord r;assert(f.snapshot(r)&&r.cause==7&&r.pc==100&&r.value==200);}
CASE(23,"NXR-073","later fault cannot overwrite cause"){FirstFault f;f.capture({3});assert(!f.capture({9}));FaultRecord r;f.snapshot(r);assert(r.cause==3);}
CASE(24,"NXR-073","status captured verbatim"){FirstFault f;f.capture({0,0,0,0xdeadbeef});FaultRecord r;f.snapshot(r);assert(r.status==0xdeadbeef);}
CASE(25,"NXR-073","seed worker sequence retained"){FirstFault f;f.capture({0,0,0,0,123,2,789});FaultRecord r;f.snapshot(r);assert(r.seed==123&&r.worker==2&&r.sequence==789);}
CASE(26,"NXR-073","zero-valued record remains present"){FirstFault f;assert(f.capture({}));FaultRecord r{9};assert(f.snapshot(r)&&!r.cause&&!f.capture({1}));}
CASE(27,"NXR-073","fault snapshot is a copy"){FirstFault f;FaultRecord a{1},b;f.capture(a);a.cause=8;f.snapshot(b);b.cause=9;f.snapshot(a);assert(a.cause==1);}
CASE(28,"NXR-073","ISR capture restores nested mask"){FirstFault f;model::isr=true;model::depth=2;assert(f.capture({5})&&model::depth==2);model::depth=0;model::isr=false;}
CASE(29,"NXR-073","independent record instances"){FirstFault a,b;a.capture({1});b.capture({2});FaultRecord r;a.snapshot(r);assert(r.cause==1);b.snapshot(r);assert(r.cause==2);}
CASE(30,"NXR-073","capture never asks scheduler to run"){FirstFault f;auto n=model::reschedules;f.capture({});FaultRecord r;f.snapshot(r);assert(model::reschedules==n);}
