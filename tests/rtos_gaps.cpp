/** @file tests/rtos_gaps.cpp
 * @brief Verifies NexRT RTOS gap closures:
 *   - StreamBuffer<Capacity> (thread + ISR producer/consumer, wraparound, blocking, drops)
 *   - MessageBuffer<Capacity> (atomic variable-length framing, overflow safety, ISR wakeup)
 *   - TimerTable<MaxTimers> (one-shot, periodic, 32-bit tick rollover, next_delay)
 *   - watchdog::Supervisor<MaxTasks> + CRC-32C CrashRecord
 *   - Scheduler::next_wake_delay() (tickless idle horizon) and notification timeout expiry
 */
#include "sync_model.hpp"
#include "rtos_stream.hpp"
#include "rtos_timers.hpp"
#include "rtos_watchdog.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace rtos;

namespace {

static Thread producer, consumer;
static StreamBuffer<32> stream;
static MessageBuffer<40> msgbuf;

struct TimerCounter {
    uint32_t oneshot{};
    uint32_t periodic{};
};

static void on_oneshot(void* arg) { ++static_cast<TimerCounter*>(arg)->oneshot; }
static void on_periodic(void* arg) { ++static_cast<TimerCounter*>(arg)->periodic; }
static void pet_wdt(void* arg) { ++(*static_cast<uint32_t*>(arg)); }

static void isr_send_stream() {
    model::isr = true;
    size_t iw = 0;
    assert(stream.try_send_from_isr("ISR!", 4, iw) == Result::ok);
    assert(iw == 4);
    model::isr = false;
}

static void isr_send_msg() {
    model::isr = true;
    assert(msgbuf.try_send_from_isr("FRAME_WRAP_OK!", 14) == Result::ok);
    model::isr = false;
}

void test_stream_buffer() {
    model::scheduler = {};
    model::isr = false;
    model::started = true;
    model::on_park = nullptr;
    model::use(producer, 2);
    model::scheduler.add(producer);
    model::scheduler.add(consumer);

    // Send & wraparound test
    const char part1[] = "0123456789ABCDEF0123"; // 20 bytes
    size_t written = 0;
    assert(stream.send(part1, 20, written) == Result::ok);
    assert(written == 20);

    // Partial write when nearly full increments drops
    const char part2[] = "abcdefghijklmnop"; // 16 bytes -> only 12 fit
    assert(stream.send(part2, 16, written) == Result::ok);
    assert(written == 12);
    auto info = stream.snapshot();
    assert(info.used == 32 && info.high_water == 32 && info.drops == 1);

    // Full stream rejects further bytes
    assert(stream.send("X", 1, written) == Result::full);
    assert(written == 0);

    // Read 24 bytes, leaving 8, then write 16 bytes across ring wraparound
    char rx[32]{};
    size_t got = 0;
    assert(stream.receive(rx, 24, got) == Result::ok);
    assert(got == 24);
    assert(std::memcmp(rx, "0123456789ABCDEF0123abcd", 24) == 0);

    const char part3[] = "WRAP_AROUND_DATA";
    assert(stream.send(part3, 16, written) == Result::ok);
    assert(written == 16);

    assert(stream.receive(rx, 24, got) == Result::ok);
    assert(got == 24);
    assert(std::memcmp(rx, "efghijklWRAP_AROUND_DATA", 24) == 0);

    // Blocking receive woken from ISR
    model::use(consumer, 4);
    model::on_park = isr_send_stream;
    assert(stream.receive(rx, 8, got, 10) == Result::ok);
    assert(got == 4 && std::memcmp(rx, "ISR!", 4) == 0);
    model::on_park = nullptr;

    // Context check: ISR cannot call thread send()
    model::isr = true;
    assert(stream.send("Z", 1, written) == Result::context);
    model::isr = false;
}

void test_message_buffer() {
    model::scheduler = {};
    model::isr = false;
    model::started = true;
    model::on_park = nullptr;
    model::use(producer, 2);
    model::scheduler.add(producer);
    model::scheduler.add(consumer);

    const char m1[] = "hello";
    const char m2[] = "world-1234567890";
    assert(msgbuf.send(m1, 5) == Result::ok);
    assert(msgbuf.send(m2, 16) == Result::ok);

    // Third message won't fit (used = 7 + 18 = 25, capacity = 40, need 20)
    const char m3[] = "012345678901234567";
    assert(msgbuf.send(m3, 18) == Result::full);
    assert(msgbuf.snapshot().drops == 1);

    // Too-small receive buffer returns overflow and preserves the message intact!
    char buf[32]{};
    size_t len = 0;
    assert(msgbuf.receive(buf, 3, len) == Result::overflow);
    assert(msgbuf.receive(buf, sizeof(buf), len) == Result::ok);
    assert(len == 5 && std::memcmp(buf, m1, 5) == 0);

    assert(msgbuf.receive(buf, sizeof(buf), len) == Result::ok);
    assert(len == 16 && std::memcmp(buf, m2, 16) == 0);
    assert(msgbuf.receive(buf, sizeof(buf), len) == Result::empty);

    // ISR producer wakes blocked reader across wraparound
    model::use(consumer, 5);
    model::on_park = isr_send_msg;
    assert(msgbuf.receive(buf, sizeof(buf), len, 20) == Result::ok);
    assert(len == 14 && std::memcmp(buf, "FRAME_WRAP_OK!", 14) == 0);
    model::on_park = nullptr;
}

void test_timers() {
    TimerTable<4> timers;
    TimerCounter cnt{};
    size_t h_one = 0;
    size_t h_per = 0;
    assert(timers.create(h_one, on_oneshot, &cnt, 10, false));
    assert(timers.create(h_per, on_periodic, &cnt, 5, true));

    // Start near 32-bit wraparound boundary!
    const Tick t0 = 0xFFFFFFFAu;
    assert(timers.start(h_one, t0));
    assert(timers.start(h_per, t0));
    assert(timers.next_delay(t0) == 5u);

    // Advance tick-by-tick across 0xFFFFFFFF -> 0x00000000 rollover
    for (uint32_t step = 1; step <= 12; ++step) {
        (void)timers.tick(static_cast<Tick>(t0 + step));
    }
    assert(cnt.oneshot == 1u);
    assert(!timers.active(h_one));
    assert(cnt.periodic == 2u); // fired at +5 and +10
    assert(timers.active(h_per));
    assert(timers.next_delay(static_cast<Tick>(t0 + 12)) == 3u);

    assert(timers.stop(h_per));
    assert(timers.next_delay(static_cast<Tick>(t0 + 12)) == 0u);
}

void test_watchdog_and_crash_record() {
    watchdog::CrashRecord retained{};
    watchdog::Supervisor<4> sup;
    uint32_t hw_pets = 0;
    assert(sup.initialize(pet_wdt, &hw_pets, &retained));

    size_t t0 = 0;
    size_t t1 = 0;
    assert(sup.register_task(t0, 50, 100));
    assert(sup.register_task(t1, 100, 100));

    // Both tasks healthy at t=140
    assert(sup.checkin(t0, 130));
    assert(sup.evaluate(140));
    assert(hw_pets == 1u && !sup.tripped() && !retained.valid());

    // Task 0 misses deadline at t=181 (elapsed 51 > 50)
    assert(!sup.evaluate(181));
    assert(hw_pets == 1u); // Hardware watchdog NOT petted!
    assert(sup.tripped() && sup.stalled_task() == t0);
    assert(retained.valid());
    assert(retained.fault_code == 1u);
    assert(retained.stalled_task_id == t0);
    assert(retained.fault_tick == 181u);
    assert(retained.last_checkin_tick == 130u);

    // Flip 1 bit in retained SRAM -> CRC-32C catches corruption immediately
    retained.fault_tick ^= 0x01u;
    assert(!retained.valid());
    retained.fault_tick ^= 0x01u;
    assert(retained.valid());
    retained.clear();
    assert(!retained.valid());
}

void test_tickless_horizon_and_notify_timeout() {
    detail::Scheduler sched;
    Thread a{}, b{};
    a.priority = a.effective_priority = 3;
    a.state = State::ready;
    b.priority = b.effective_priority = 5;
    b.state = State::ready;
    assert(sched.add(a));
    assert(sched.add(b));

    // Both threads runnable -> next_wake_delay() == 0 (cannot tickless-sleep)
    assert(sched.next_wake_delay() == 0u);

    // Put both threads to sleep: a for 25 ticks, b waiting on notification with 10-tick timeout
    a.state = State::sleeping;
    a.wake = sched.now + 25u;
    b.state = State::blocked;
    b.waiting_notify = true;
    b.wake = sched.now + 10u;

    assert(sched.next_wake_delay() == 10u);

    // Advance 9 ticks: b still waiting
    sched.advance(9u);
    assert(b.state == State::blocked && b.waiting_notify);
    assert(sched.next_wake_delay() == 1u);

    // 10th tick expires notification wait cleanly!
    sched.advance(1u);
    assert(b.state == State::ready && !b.waiting_notify);
    assert(b.wait.result == detail::WaitResult::timeout);
    assert(sched.next_wake_delay() == 0u);
}

} // namespace

int main() {
    test_stream_buffer();
    test_message_buffer();
    test_timers();
    test_watchdog_and_crash_record();
    test_tickless_horizon_and_notify_timeout();
    std::printf("PASS: rtos_gaps (StreamBuffer, MessageBuffer, TimerTable, Watchdog+CRC32C, Tickless+NotifyTimeout)\n");
    return 0;
}
