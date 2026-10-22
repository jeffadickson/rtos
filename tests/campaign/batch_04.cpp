#include "common.hpp"
#include "rtos_mmio.hpp"
using namespace rtos;
struct Bus {uint32_t value=0x12345678;unsigned reads{},writes{};uintptr_t last{};uint32_t read(uintptr_t a){last=a;reads++;return value;}void write(uintptr_t a,uint32_t v){last=a;writes++;value=v;}};
CASE(31,"NXR-117","read-only register performs one word read"){Bus b;Register32<RegisterAccess::read_only,Bus> r(b,4);uint32_t v;assert(r.read(v)&&v==0x12345678&&b.reads==1&&b.last==4);}
CASE(32,"NXR-117","read-only write never reaches bus"){Bus b;Register32<RegisterAccess::read_only,Bus> r(b,4);assert(!r.write(0)&&!b.writes);}
CASE(33,"NXR-117","write-only read preserves caller output"){Bus b;Register32<RegisterAccess::write_only,Bus> r(b,4);uint32_t v=99;assert(!r.read(v)&&v==99&&!b.reads);}
CASE(34,"NXR-117","write-only exact value and address"){Bus b;Register32<RegisterAccess::write_only,Bus> r(b,8);assert(r.write(0xdeadbeef)&&b.value==0xdeadbeef&&b.last==8&&b.writes==1&&!b.reads);}
CASE(35,"NXR-117","read-write masked update preserves unrelated bits"){Bus b;Register32<RegisterAccess::read_write,Bus> r(b,4);assert(r.update(0xf0,0xa0)&&b.value==0x123456a8&&b.reads==1&&b.writes==1);}
CASE(36,"NXR-117","unaligned read fails unchanged"){Bus b;Register32<RegisterAccess::read_write,Bus> r(b,1);uint32_t v=9;assert(!r.read(v)&&v==9&&!b.reads);}
CASE(37,"NXR-117","unaligned writes have no bus effect"){Bus b;Register32<RegisterAccess::read_write,Bus> r(b,2);assert(!r.write(0)&&!r.update(1,2)&&!b.reads&&!b.writes);}
CASE(38,"NXR-117","W1C writes only requested clear bits"){Bus b;Register32<RegisterAccess::write_one_to_clear,Bus> r(b,4);assert(r.write(8)&&b.value==8&&!b.reads&&b.writes==1);}
CASE(39,"NXR-117","W1C refuses destructive read-modify-write"){Bus b;Register32<RegisterAccess::write_one_to_clear,Bus> r(b,4);assert(!r.update(1,2)&&!b.reads&&!b.writes);}
CASE(40,"NXR-117","read-only update never reads side-effect register"){Bus b;Register32<RegisterAccess::read_only,Bus> r(b,4);assert(!r.update(1,2)&&!b.reads&&!b.writes);}
