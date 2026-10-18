#pragma once
#include <stddef.h>
#include <stdint.h>
namespace rtos::net {
constexpr size_t frame_capacity = 1536; // Untagged/tagged standard frames, no FCS.
enum class Io : uint8_t { ok, empty, busy, error };
struct Mac {
    void* context{};
    // RX copies one complete frame and returns its byte count; zero-copy DMA stays in the driver.
    Io (*receive)(void*, uint8_t*, size_t capacity, size_t* length){};
    // TX must consume/copy before returning; busy means caller may retry.
    Io (*transmit)(void*, const uint8_t*, size_t length){};
    bool (*link_up)(void*){};
};
struct Statistics { uint64_t received{}, transmitted{}, dropped{}, errors{}, busy{}; };
// One owning network thread; never invoke from ISR, concurrently, or reentrantly.
// Callback may inspect/copy frame only during the call; may not retain its pointer.
class Ethernet {
public:
    using Input = bool (*)(void*, const uint8_t*, size_t);
    bool initialize(Mac mac, Input input, void* stack_context);
    size_t poll(size_t budget = 4);
    Io send(const uint8_t* frame, size_t length);
    bool link_up() const;
    Statistics statistics() const { return stats_; }
private:
    Mac mac_{};
    Input input_{};
    void* stack_context_{};
    Statistics stats_{};
    uint8_t frame_[frame_capacity]{};
    bool initialized_{};
};
}
