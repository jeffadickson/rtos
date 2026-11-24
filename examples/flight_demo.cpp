/** @file examples/flight_demo.cpp
 * @brief Flight-safe RV32IM reference firmware demonstrating RTOS_FLIGHT_BUILD=ON
 *        with Tier 1 services (BootHandoff, RateMonotonicPeriod, FixedPool,
 *        WorkQueue, Barrier, EpochClock, Tickless Idle), Topic, TraceRing,
 *        and JournalFs storage — zero heap, zero monitor, zero ELF loader.
 */
#include "rtos.hpp"
#include "rtos_boot.hpp"
#include "rtos_clock.hpp"
#include "rtos_flash_mocks.hpp"
#include "rtos_pool.hpp"
#include "rtos_rm.hpp"
#include "rtos_storage.hpp"
#include "rtos_topic.hpp"
#include "rtos_trace.hpp"
#include "rtos_workqueue.hpp"

namespace {

struct TelemetryMsg {
    uint32_t seq{};
    uint32_t ticks{};
};

rtos::boot::BootHandoff retained_handoff{};
rtos::watchdog::CrashRecord retained_crash{};
rtos::boot::HealthManager health{};
rtos::RateMonotonicPeriod rm_control{};
rtos::FixedPool<32, 4> packet_pool{};
rtos::WorkQueue<4> work_queue{};
rtos::Barrier<2> startup_barrier{};
rtos::EpochClock epoch_clock{};
rtos::Topic<TelemetryMsg, 2> telem_topic{};
rtos::trace::TraceRing<16> flight_trace{};
rtos::storage::mocks::SerialNorMock<4, 2, 128> nor_mock{};
rtos::storage::SerialNorAdapter nor_adapter{};
rtos::storage::JournalFs<4, 2, 128> vault{};

rtos::Thread t_ctrl, t_work;
rtos::Stack<1024> s_ctrl, s_work;
volatile uint32_t telem_observed{};

void on_telem(const TelemetryMsg& msg, void*) {
    telem_observed = msg.seq;
    flight_trace.record(msg.ticks, rtos::trace::ev_user_base, 1u, msg.seq);
}

void deferred_work(void* arg) {
    auto* blk = arg;
    (void)packet_pool.release(blk);
}

void control_task(void*) {
    (void)startup_barrier.wait(50u);
    (void)health.confirm_boot_healthy();
    uint32_t seq = 0;
    for (;;) {
        (void)rm_control.wait(10u, rtos::ticks());
        ++seq;
        telem_topic.publish({seq, rtos::ticks()});
        void* blk = nullptr;
        if (packet_pool.allocate(blk, rtos::NoWait) == rtos::Result::ok) {
            if (work_queue.submit(deferred_work, blk) != rtos::Result::ok) {
                (void)packet_pool.release(blk);
            }
        }
    }
}

void worker_task(void*) {
    (void)startup_barrier.wait(50u);
    for (;;) {
        (void)work_queue.poll_one(rtos::ticks(), 10u);
    }
}

} // namespace

int main() {
    rtos::boot::bootloader_prepare_handoff(retained_handoff, rtos::boot::BootReason::power_on, 0xF1160001u, 3u);
    (void)health.initialize(&retained_handoff, &retained_crash);
    (void)epoch_clock.initialize(RTOS_TICK_HZ);
    (void)telem_topic.subscribe(on_telem, nullptr);
    (void)nor_adapter.initialize(nor_mock.spi_bus(), 4u, 2u, 128u);
    (void)vault.mount(nor_adapter.backend());
    const uint32_t boot_tag = retained_handoff.boot_count;
    (void)vault.write_file(0u, &boot_tag, sizeof(boot_tag));

    (void)rtos::create(t_ctrl, control_task, nullptr, 4, s_ctrl.words, sizeof(s_ctrl), "ctrl");
    (void)rtos::create(t_work, worker_task, nullptr, 2, s_work.words, sizeof(s_work), "work");
    rtos::start(50000000u, RTOS_TICK_HZ);
}
