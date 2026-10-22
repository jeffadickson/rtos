/** @file Compile/run the HAL with a test processor that uses distinct exception identities. */
#include "rtos_hal.hpp"
#include <assert.h>
static void entry(){}
int main(){
    const auto outer=rtos::hal::mask_interrupts();assert(outer==7&&rtos::port::mask==0);
    const auto inner=rtos::hal::mask_interrupts();assert(inner==0);
    rtos::hal::restore_interrupts(inner);assert(rtos::port::mask==0);
    rtos::hal::restore_interrupts(outer);assert(rtos::port::mask==7);
    rtos::port::clock=uint64_t(1)<<40;
    assert(rtos::hal::time_now()==uint64_t(1)<<40);
    rtos::hal::arm(rtos::hal::time_now()+17);assert(rtos::port::deadline==(uint64_t(1)<<40)+17);
    uint32_t storage[32]{};
    auto* frame=rtos::hal::initialize_context(storage,sizeof(storage),entry);
    assert(frame==storage+28&&rtos::port::entry_seen==entry);
    rtos::hal::resume_after_yield(frame);assert(frame[0]==8);
    assert(rtos::hal::is_yield(91)&&!rtos::hal::is_tick(91));
    assert(rtos::hal::is_tick(92)&&!rtos::hal::is_yield(92));
    assert(!rtos::hal::is_yield(11)&&!rtos::hal::is_tick(0x80000007));
    rtos::hal::install_vector();rtos::hal::enable_timer();rtos::hal::reschedule();rtos::hal::idle();
    assert(rtos::port::vectors==1&&rtos::port::timers==1&&rtos::port::schedules==1&&rtos::port::idles==1);
}
