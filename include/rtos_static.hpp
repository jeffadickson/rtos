#pragma once
#include <stddef.h>
#include <stdint.h>
namespace rtos::static_storage {
enum class Result : uint8_t { ok, invalid, empty, full, stale, busy, timeout, unsupported, quarantined };
struct Bytes { const uint8_t* data{};size_t size{};bool valid() const{return data||size==0;} };
struct MutableBytes { uint8_t* data{};size_t size{};bool valid() const{return data||size==0;} };
inline bool checked_add(size_t a,size_t b,size_t& out){if(b>SIZE_MAX-a)return false;out=a+b;return true;}
inline void saturate(uint32_t& value){if(value!=UINT32_MAX)++value;}
template<class T,size_t Capacity> class Ring {
    static_assert(Capacity>0);T slots[Capacity]{};size_t head{},used{};uint32_t drops_{};
public:
    Result push(const T& value){if(used==Capacity){saturate(drops_);return Result::full;}slots[(head+used)%Capacity]=value;++used;return Result::ok;}
    Result pop(T& value){if(!used)return Result::empty;value=slots[head];head=(head+1)%Capacity;--used;return Result::ok;}
    const T* peek()const{return used?&slots[head]:nullptr;}
    size_t size()const{return used;}static constexpr size_t capacity(){return Capacity;}
    uint32_t drops()const{return drops_;}void clear(){head=used=0;}
};
struct Token { uint16_t slot{};uint32_t generation{}; };
template<size_t Count,size_t BytesPerBlock> class Pool {
    static_assert(Count>0&&Count<=65535&&BytesPerBlock>0);
    alignas(16) uint8_t data_[Count][BytesPerBlock]{};uint32_t generation_[Count]{};bool loaned_[Count]{},quarantined_[Count]{};
public:
    Result acquire(Token& token,MutableBytes& bytes){for(size_t i=0;i<Count;++i)if(!loaned_[i]&&!quarantined_[i]){if(generation_[i]==UINT32_MAX){quarantined_[i]=true;continue;}loaned_[i]=true;token={uint16_t(i),++generation_[i]};bytes={data_[i],BytesPerBlock};return Result::ok;}return Result::full;}
    Result release(Token token){if(token.slot>=Count)return Result::invalid;if(!loaned_[token.slot]||generation_[token.slot]!=token.generation)return Result::stale;loaned_[token.slot]=false;return Result::ok;}
    Result quarantine(Token token){if(token.slot>=Count||!loaned_[token.slot]||generation_[token.slot]!=token.generation)return Result::stale;loaned_[token.slot]=false;quarantined_[token.slot]=true;return Result::quarantined;}
    bool conserved()const{size_t total=0;for(size_t i=0;i<Count;++i){if(loaned_[i]&&quarantined_[i])return false;++total;}return total==Count;}
};
// Containers require a single owner or an external lock; they are not ISR/SMP synchronization primitives.
}
