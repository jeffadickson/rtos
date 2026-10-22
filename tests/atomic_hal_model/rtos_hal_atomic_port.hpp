#pragma once
/** @file Test-only atomic provider. Models HAL routing, not hardware concurrency. */
#include <stdint.h>
#include <assert.h>
namespace rtos::hal::atomic {
inline unsigned exchanges{},adds{},ors{},ands{};
inline bool available(){return true;}
inline uint32_t exchange(uint32_t* word,uint32_t value){
    assert(model::depth==0);++exchanges;auto old=*word;*word=value;return old;
}
inline uint32_t fetch_add(uint32_t* word,uint32_t value){
    assert(model::depth==0);++adds;auto old=*word;*word+=value;return old;
}
inline uint32_t fetch_or(uint32_t* word,uint32_t value){
    assert(model::depth==0);++ors;auto old=*word;*word|=value;return old;
}
inline uint32_t fetch_and(uint32_t* word,uint32_t value){
    assert(model::depth==0);++ands;auto old=*word;*word&=value;return old;
}
}
