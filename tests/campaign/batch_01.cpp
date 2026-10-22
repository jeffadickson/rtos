#include "common.hpp"
#include "rtos_periodic.hpp"
using rtos::PeriodicRelease;
CASE(1,"NXR-012","zero period preserves deadline and output"){PeriodicRelease p{10,0};uint32_t d=77;assert(!p.advance(10,d)&&p.next==10&&d==77);}
CASE(2,"NXR-012","half-range period rejected"){PeriodicRelease p{10,0x80000000};uint32_t d=77;assert(!p.advance(10,d)&&d==77);}
CASE(3,"NXR-012","before release preserves grid"){PeriodicRelease p{100,10};uint32_t d;assert(p.advance(97,d)&&d==3&&p.next==100&&!p.missed);}
CASE(4,"NXR-012","exact release advances one period"){PeriodicRelease p{100,10};uint32_t d;assert(p.advance(100,d)&&d==10&&p.next==110&&!p.missed);}
CASE(5,"NXR-012","short overrun keeps original grid"){PeriodicRelease p{100,10};uint32_t d;assert(p.advance(103,d)&&d==7&&p.next==110&&!p.missed);}
CASE(6,"NXR-012","multiple releases count misses"){PeriodicRelease p{100,10};uint32_t d;assert(p.advance(135,d)&&d==5&&p.next==140&&p.missed==3);}
CASE(7,"NXR-012","deadline wraps across uint32 boundary"){PeriodicRelease p{0xfffffffe,5};uint32_t d;assert(p.advance(0xfffffffe,d)&&d==5&&p.next==3);}
CASE(8,"NXR-012","late release after wrap counts misses"){PeriodicRelease p{0xfffffffe,5};uint32_t d;assert(p.advance(9,d)&&d==4&&p.next==13&&p.missed==2);}
CASE(9,"NXR-012","miss counter saturates"){PeriodicRelease p{0,1,UINT32_MAX-1};uint32_t d;assert(p.advance(10,d)&&p.missed==UINT32_MAX&&d==1);}
CASE(10,"NXR-012","ambiguous half-range observation rejected unchanged"){PeriodicRelease p{0,1};uint32_t d=9;assert(!p.advance(0x80000000,d)&&p.next==0&&d==9);}
