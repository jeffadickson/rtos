#pragma once
#include <stdint.h>
namespace rtos {
struct Thread;
class Mutex;
namespace detail {
class WaitQueue;
constexpr uint8_t wait_all_bits=1;
constexpr uint8_t consume_bits=2;
enum class WaitResult : uint8_t { ok, invalid, full, empty, timeout, context, overflow, owner_dead, deadlock, busy };
// All links belong to the kernel; one blocking operation per permanent thread.
struct WaitNode {
    WaitQueue* queue{};
    Thread* next{};
    void* data{};
    uint32_t mask{}, observed{};
    uint8_t flags{};
    WaitResult result{WaitResult::ok};
};
}
}
