#pragma once
/** @file
 * @brief Allocation-free stack paint and budgeted prefix scanning.
 * Internal callers validate alignment, lifetime and extent before using these
 * helpers. Matching paint is evidence of unwritten bytes, not proof of spare
 * stack capacity: saved values may equal paint and reserved frames may be unwritten.
 */
#include <stddef.h>
#include <stdint.h>
namespace rtos::detail {
inline constexpr uint32_t stack_paint=0xa5a5a5a5u;
/// Boot only, before the port constructs its initial frame. No active stack here.
inline void paint_stack(uint32_t* words,size_t count){
    for(size_t i=0;i<count;++i)words[i]=stack_paint;
}
struct StackScan {size_t read_words{},untouched_words{};bool complete{};};
/// Inspect at most budget_words, stopping at the first non-paint word.
/// Volatile reads observe real stack memory; caller serializes the owning CPU.
inline StackScan scan_stack(const volatile uint32_t* words,size_t count,size_t budget_words){
    if(count==0)return {0,0,true};
    if(!words)return {}; // Fail closed for an invalid internal nonempty extent.
    StackScan result{};
    const size_t limit=budget_words<count?budget_words:count;
    while(result.read_words<limit){
        const auto value=words[result.read_words++];
        if(value!=stack_paint){result.complete=true;return result;}
        ++result.untouched_words;
    }
    result.complete=result.untouched_words==count;
    return result;
}
}
