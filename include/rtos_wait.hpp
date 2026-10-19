#pragma once
#include <stdint.h>
namespace rtos {
struct Thread;
class Mutex;
namespace detail {
class WaitQueue;
enum class WaitResult : uint8_t { ok, invalid, full, empty, timeout, context, overflow, owner_dead, deadlock, busy };
// All links belong to the kernel; one blocking operation per permanent thread.
struct WaitNode {
    WaitQueue* queue{};
    Thread* next{};
    void* data{};
    uint32_t mask{}, observed{};
    uint8_t flags{};
    bool timed{};
    WaitResult result{WaitResult::ok};
};
}
}
