/** @file
 * @brief Verifies compile-time and runtime guarantees of rtos_safe.hpp:
 *   - OrderedLock strict ascending hierarchy (deadlock impossible by construction)
 *   - CeilingResource Immediate Priority Ceiling Protocol (IPCP) priority elevation & rejection
 *   - TypedChannel capability separation (ThreadSender / ThreadReceiver / IsrSender)
 */
#include "sync_model.hpp"
#include "rtos_safe.hpp"
#include <cassert>
#include <cstdio>
#include <type_traits>

using namespace rtos;
using namespace rtos::safe;

namespace {
// Compile-time negative check: IsrSender must NOT have a blocking send() member.
template<class T, class = void>
struct has_blocking_send : std::false_type {};
template<class T>
struct has_blocking_send<T, std::void_t<decltype(std::declval<T&>().send(0u, 1u))>> : std::true_type {};

static_assert(!has_blocking_send<TypedChannel<uint32_t, 4>::IsrSender>::value,
              "IsrSender must not expose blocking send()");
static_assert(has_blocking_send<TypedChannel<uint32_t, 4>::ThreadSender>::value,
              "ThreadSender must expose blocking send()");

static Thread worker, high_worker;
static OrderedLock<uint32_t, 1> outer_lock(10u);
static OrderedLock<uint32_t, 2> inner_lock(20u);
static CeilingResource<uint32_t, 5> ipcp_bus(0u);
static TypedChannel<uint32_t, 4> channel;
} // namespace

int main() {
    model::use(worker, 2);
    model::scheduler.add(worker);
    model::scheduler.add(high_worker);

    // 1. OrderedLock strict hierarchy: level 0 -> level 1 -> level 2 succeeds cleanly.
    const auto r1 = outer_lock.with_lock(RootLockToken{}, 10, [&](uint32_t& a, auto tok1) {
        static_assert(decltype(tok1)::level == 1);
        const auto r2 = inner_lock.with_lock(tok1, 10, [&](uint32_t& b, auto tok2) {
            static_assert(decltype(tok2)::level == 2);
            a += 5u;
            b += a;
        });
        assert(r2 == Result::ok);
    });
    assert(r1 == Result::ok);
    assert(outer_lock.snapshot().owner == nullptr);
    assert(inner_lock.snapshot().owner == nullptr);

    // 2. CeilingResource (IPCP): raises effective_priority to ceiling (5) inside access(),
    //    restores base priority (2) on exit.
    uint8_t observed_prio = 0;
    const auto rc = ipcp_bus.access(10, [&](uint32_t& v) {
        observed_prio = worker.effective_priority;
        v = 42u;
    });
    assert(rc == Result::ok);
    assert(observed_prio == 5);
    assert(worker.effective_priority == 2);
    assert(ipcp_bus.snapshot().owner == nullptr);

    // Caller with base priority > ceiling (8 > 5) must be rejected fail-closed.
    model::use(high_worker, 8);
    assert(ipcp_bus.access(10, [](uint32_t&) {}) == Result::invalid);

    // 3. Capability-separated TypedChannel: ISR sends via IsrSender, thread receives via ThreadReceiver.
    auto isr_tx = channel.isr_sender();
    auto thr_rx = channel.thread_receiver();
    model::isr = true;
    assert(isr_tx.try_send(0x1234u) == Result::ok);
    model::isr = false;
    model::use(worker, 2);
    uint32_t out = 0;
    assert(thr_rx.receive(out, 0) == Result::ok && out == 0x1234u);

    std::printf("SAFE_CONCURRENCY_PASS\n");
    return 0;
}
