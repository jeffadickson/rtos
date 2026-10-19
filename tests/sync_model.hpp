#pragma once
#include "rtos_sync.hpp"
#include "scheduler.hpp"
#include <assert.h>
namespace model {
inline rtos::detail::Scheduler scheduler;
inline rtos::Thread* caller{};
inline bool isr{},started=true;
inline unsigned depth{},reschedules{};
inline void (*on_park)(){};
inline void use(rtos::Thread& t,uint8_t priority=1){t.state=rtos::State::ready;t.priority=t.effective_priority=priority;caller=&t;}
}
namespace rtos::detail {
CriticalState critical_enter(){return model::depth++;}
void critical_leave(CriticalState saved){assert(model::depth==saved+1);model::depth=unsigned(saved);}
Thread* calling_thread(){return model::started&&!model::isr?model::caller:nullptr;}
bool interrupt_context(){return model::isr;}
bool synchronization_running(){return model::started;}
Tick synchronization_now(){return model::scheduler.now;}
void park_thread(){assert(model::depth>0&&model::on_park);auto* saved=model::caller;model::on_park();model::caller=saved;assert(saved->state==State::ready);}
void schedule_ready(){++model::reschedules;}
}
