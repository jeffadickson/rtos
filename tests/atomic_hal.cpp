/** @file Check optional HAL routing, prior values and preserved UP compare/swap semantics. */
#include "sync_model.hpp"
#include "rtos_atomic.hpp"
using namespace rtos;
int main(){
    static_assert(sizeof(AtomicU32)==sizeof(uint32_t));
    AtomicU32 word(UINT32_MAX);
    assert(word.fetch_add(1)==UINT32_MAX&&word.load()==0);
    word.store(6);assert(word.exchange(9)==6);
    assert(word.fetch_or(16)==9&&word.fetch_and(15)==25&&word.load()==9);
    uint32_t expected=9;assert(word.compare_exchange(expected,3)&&expected==9);
    expected=9;assert(!word.compare_exchange(expected,7)&&expected==3&&word.load()==3);
    assert(hal::atomic::exchanges==2&&hal::atomic::adds==1&&hal::atomic::ors==1&&hal::atomic::ands==1);
    uint32_t reference=3,random=42;
    for(unsigned step=0;step<10000;++step){
        random^=random<<13;random^=random>>17;random^=random<<5;
        const auto old=reference;
        switch(step%4){
        case 0:reference+=random;assert(word.fetch_add(random)==old);break;
        case 1:reference|=random;assert(word.fetch_or(random)==old);break;
        case 2:reference&=random;assert(word.fetch_and(random)==old);break;
        default:reference=random;assert(word.exchange(random)==old);break;
        }
        assert(word.load()==reference&&model::depth==0);
    }
}
