#pragma once
#include "rtos_ethernet.hpp"
namespace rtos::net {
/** Modern virtio-MMIO access seam. QEMU uses ordered device registers; tests
 * substitute a device model. All calls and queue memory belong to one worker.
 */
struct VirtioIo {
    void* context{};
    uint32_t (*read)(void*,uint32_t offset){};
    void (*write)(void*,uint32_t offset,uint32_t value){};
};
namespace virtio {
// VERSION_1 uses the 12-byte modern header, even without mergeable RX.
constexpr unsigned queue_size=8,header_bytes=12;
struct alignas(16) Descriptor {uint64_t address{};uint32_t length{};uint16_t flags{},next{};};
struct Available {uint16_t flags{},index{},ring[queue_size]{},unused_event{};};
struct UsedElement {uint32_t id{},length{};};
struct Used {uint16_t flags{},index{};UsedElement ring[queue_size]{};uint16_t unused_event{};};
struct Queue {Descriptor descriptors[queue_size]{};Available available{};alignas(4) Used used{};};
static_assert(sizeof(Descriptor)==16&&sizeof(UsedElement)==8);
}
struct VirtioStatistics {uint32_t received{},transmitted{},busy{},errors{};};
/** Boot-static modern split-ring virtio net, no offloads or mergeable buffers.
 * Eight permanently posted RX buffers and one copied outstanding TX. CPU and
 * device see coherent identity-mapped RAM in the QEMU RV32 profile only. A used
 * completion releases device ownership; an invalid completion fails closed.
 * Initialization is bounded and one-shot. No external IRQ/PLIC support claimed.
 */
class VirtioNet {
    VirtioIo io_{};virtio::Queue receive_{},transmit_{};
    alignas(16) uint8_t receive_bytes_[virtio::queue_size][frame_capacity+virtio::header_bytes]{};
    alignas(16) uint8_t transmit_bytes_[frame_capacity+virtio::header_bytes]{};
    uint16_t receive_used_{},transmit_used_{};bool attempted_{},ready_{},transmitting_{};
    VirtioStatistics statistics_{};
    uint32_t read(uint32_t offset)const;
    void write(uint32_t offset,uint32_t value);
    bool fail();
    bool configure_queue(unsigned index,virtio::Queue& queue);
    bool reap_transmit();
    void repost(uint16_t id);
public:
    constexpr VirtioNet()=default;
    VirtioNet(const VirtioNet&)=delete;
    VirtioNet& operator=(const VirtioNet&)=delete;
    bool initialize(VirtioIo io);
    /// Map one aligned QEMU virtio-MMIO slot. Never probe arbitrary host memory.
    bool initialize_mmio(uintptr_t base);
    Io receive(uint8_t* bytes,size_t capacity,size_t* length);
    Io transmit(const uint8_t* bytes,size_t length);
    bool link_up()const{return ready_;}
    VirtioStatistics statistics()const{return statistics_;}
    Mac mac();
};
}
