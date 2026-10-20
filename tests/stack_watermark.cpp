#include "rtos/detail/stack_watermark.hpp"
#include <cassert>
using namespace rtos::detail;
int main(){
    // 016: zero-length paint is valid even without a buffer.
    paint_stack(nullptr,0);
    // 017: empty scans complete without dereferencing a null pointer.
    auto scan=scan_stack(nullptr,0,0);assert(scan.complete&&scan.read_words==0&&scan.untouched_words==0);
    uint32_t words[8]{};paint_stack(words,8);
    // 018: painting covers the complete validated extent.
    for(auto word:words)assert(word==stack_paint);
    // 019: a zero budget does no paint reads on nonempty storage.
    scan=scan_stack(words,8,0);assert(!scan.complete&&scan.read_words==0&&scan.untouched_words==0);
    // 020: a single-word budget is respected.
    scan=scan_stack(words,8,1);assert(!scan.complete&&scan.read_words==1&&scan.untouched_words==1);
    // 021: a short budget yields a deliberately incomplete estimate.
    scan=scan_stack(words,8,3);assert(!scan.complete&&scan.read_words==3&&scan.untouched_words==3);
    // 022: an exact full budget completes an entirely painted stack.
    scan=scan_stack(words,8,8);assert(scan.complete&&scan.read_words==8&&scan.untouched_words==8);
    // 023: an oversized budget never expands the actual extent.
    scan=scan_stack(words,8,SIZE_MAX);assert(scan.complete&&scan.read_words==8);
    // 024: a changed first word ends the scan immediately.
    words[0]=0;scan=scan_stack(words,8,8);assert(scan.complete&&scan.read_words==1&&scan.untouched_words==0);
    // 025: one-word scans can detect that first mismatch.
    scan=scan_stack(words,8,1);assert(scan.complete&&scan.read_words==1);
    paint_stack(words,8);words[3]=0;
    // 026: a mismatch just beyond the budget remains unread.
    scan=scan_stack(words,8,3);assert(!scan.complete&&scan.read_words==3&&scan.untouched_words==3);
    // 027: including the boundary mismatch marks the result complete.
    scan=scan_stack(words,8,4);assert(scan.complete&&scan.read_words==4&&scan.untouched_words==3);
    // 028: scanning does not modify the inspected memory.
    assert(words[3]==0&&words[2]==stack_paint&&words[4]==stack_paint);
    // 029: an isolated change at the top leaves the lower prefix observable.
    paint_stack(words,8);words[7]=1;scan=scan_stack(words,8,8);
    assert(scan.complete&&scan.untouched_words==7&&scan.read_words==8);
    // 030: data equal to paint is deliberately indistinguishable from unwritten memory.
    words[7]=stack_paint;scan=scan_stack(words,8,8);assert(scan.complete&&scan.untouched_words==8);
    // 031: passing a guarded subextent excludes the guard from paint observations.
    words[0]=0x51accede;scan=scan_stack(words+1,7,7);assert(scan.complete&&scan.untouched_words==7);
    // 032: a changed word after the guard is detected at subextent offset zero.
    words[1]=0;scan=scan_stack(words+1,7,7);assert(scan.complete&&scan.untouched_words==0);
    // 033: re-painting restores the initial observation without hidden state.
    paint_stack(words,8);scan=scan_stack(words,8,8);assert(scan.complete&&scan.untouched_words==8);
    // 034: exhaust every mismatch position and every possible small read budget.
    for(size_t changed=0;changed<8;++changed){
        paint_stack(words,8);words[changed]=0;
        for(size_t budget=0;budget<=9;++budget){
            scan=scan_stack(words,8,budget);
            assert(scan.read_words<=(budget<8?budget:8));
            assert(scan.untouched_words==(budget<=changed?budget:changed));
            assert(scan.complete==(budget>changed));
        }
    }
    // 035: empty subextents complete regardless of a very large read allowance.
    scan=scan_stack(nullptr,0,SIZE_MAX);assert(scan.complete&&scan.read_words==0);
    // Checkpoint repair: reject invalid nonempty null extents without reads.
    scan=scan_stack(nullptr,1,1);assert(!scan.complete&&scan.read_words==0);
}
