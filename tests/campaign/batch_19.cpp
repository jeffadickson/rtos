#include "common.hpp"
#include "elf_fixture.hpp"
using namespace campaign_elf;
CASE(181,"NXR-091","valid flat executable segment loads"){Fixture f;assert(f.accepts());}
CASE(182,"NXR-091","non-power-of-two ELF segment alignment rejected"){Fixture f;f.w32(80,3);assert(!f.accepts());}
CASE(183,"NXR-091","ELF segment offset-address incongruence rejected"){Fixture f;f.w32(24,4);f.w32(60,4);f.w32(64,4);f.w32(80,16);assert(!f.accepts());}
CASE(184,"NXR-091","entry outside loaded segments rejected"){Fixture f;f.w32(24,256);assert(!f.accepts());}
CASE(185,"NXR-091","entry in nonexecutable segment rejected"){Fixture f;f.w32(76,6);assert(!f.accepts());}
CASE(186,"NXR-091","image without loadable segment rejected"){Fixture f;f.w32(52,4);assert(!f.accepts());}
CASE(187,"NXR-091","empty loadable segment cannot establish entry"){Fixture f;f.w32(68,0);f.w32(72,0);assert(!f.accepts());}
CASE(188,"NXR-091","overlapping load segments rejected"){Fixture f;f.w16(44,2);f.w32(84,1);f.w32(88,128);f.w32(92,8);f.w32(96,8);f.w32(100,4);f.w32(104,4);f.w32(108,5);f.w32(112,4);assert(!f.accepts());}
CASE(189,"NXR-091","file segment larger than memory rejected"){Fixture f;f.w32(68,17);assert(!f.accepts());}
CASE(190,"NXR-091","IALIGN32 entry mismatch rejected"){Fixture f;f.w32(24,2);nexrt::rv::Profile p;p.compressed=false;assert(!f.accepts(p));}
