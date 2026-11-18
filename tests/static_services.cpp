// Integration acceptance uses the real littlefs and lwIP libraries. Assertions
// remain enabled in Release; the test intentionally starts before adapters exist.
#include "rtos_littlefs.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>

int main() {
    using namespace rtos::storage;
    static SimFlash<32,512> flash;
    flash.erase_all();
    uint8_t zero=0,ones=255;
    assert(flash.program(0,0,&zero,1)==0);
    assert(flash.program(0,0,&ones,1)!=0); // NOR cannot change zero back to one.
    assert(flash.erase(0)==0);
    static Volume volume;
    assert(!volume.mount()&&!volume.format()&&!volume.unmount());
    assert(!volume.write("x",nullptr,1)&&volume.read("x",nullptr,1)==LFS_ERR_INVAL);
    auto bad=flash.device();bad.read=nullptr;assert(!volume.bind(bad));
    bad=flash.device();bad.block_bytes=129;assert(!volume.bind(bad));
    assert(volume.bind(flash.device()));
    assert(!volume.mount()); // Blank media is never silently formatted.
    assert(volume.format());
    assert(volume.mount());
    assert(!volume.mount()&&!volume.format()); // Neither failure may erase media.
    const char text[]="NexRT static littlefs";
    assert(volume.write("state",text,sizeof(text)));
    assert(!volume.write(nullptr,text,sizeof(text))&&!volume.write("state",nullptr,1));
    assert(!volume.write("state",text,size_t(INT32_MAX)+1));
    assert(volume.read(nullptr,nullptr,0)==LFS_ERR_INVAL);
    assert(volume.read("state",nullptr,1)==LFS_ERR_INVAL);
    assert(volume.read("state",nullptr,size_t(INT32_MAX)+1)==LFS_ERR_INVAL);
    assert(volume.unmount());
    assert(volume.mount());
    std::array<char,64> result{};
    assert(volume.read("state",result.data(),result.size())==sizeof(text));
    assert(std::memcmp(text,result.data(),sizeof(text))==0);
    assert(volume.read("absent",result.data(),result.size())<0);
    assert(volume.unmount());
    assert(!volume.bind(flash.device()));
    flash.fail_after(0);
    assert(!volume.mount());
    flash.fail_after(-1);
    assert(volume.mount());
    assert(volume.unmount());
    assert(flash.read(32,0,result.data(),1)==LFS_ERR_INVAL);
    assert(flash.read(0,UINT32_MAX,result.data(),1)==LFS_ERR_INVAL);
    assert(flash.program(0,0,nullptr,1)==LFS_ERR_INVAL);
    // Model a reset by abandoning the old volume's caches and constructing a
    // fresh volume over the same NOR bytes. Small inline file replacement must
    // recover either the prior committed value or the new committed value.
    for(int cut=0;cut<64;++cut){
        static SimFlash<32,512> media;
        media.erase_all();Volume before;
        assert(before.bind(media.device())&&before.format()&&before.mount());
        const char old_value[]="previous committed value";
        const char new_value[]="updated committed value!";
        static_assert(sizeof(old_value)==sizeof(new_value));
        assert(before.write("value",old_value,sizeof(old_value)));
        media.fail_after(cut);(void)before.write("value",new_value,sizeof(new_value));
        media.fail_after(-1);Volume after;
        assert(after.bind(media.device())&&after.mount());
        char recovered[sizeof(old_value)]{};
        assert(after.read("value",recovered,sizeof(recovered))==sizeof(recovered));
        assert(!std::memcmp(recovered,old_value,sizeof(recovered))||!std::memcmp(recovered,new_value,sizeof(recovered)));
        assert(after.unmount());
    }
    std::puts("littlefs: static caches, NOR rules, persistence, injected I/O and 64 reset boundaries passed");
    return 0;
}
