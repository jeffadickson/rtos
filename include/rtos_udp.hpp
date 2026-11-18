#pragma once
#include "rtos_ethernet.hpp"
extern "C" {
#include "lwip/netif.h"
#include "lwip/udp.h"
}
namespace rtos::net {
/// Explicit host-order octets; no integer endianness or DHCP interpretation.
struct Ipv4Octets {uint8_t a{},b{},c{},d{};};
struct Ipv4Configuration {
    Ipv4Octets address{192,0,2,2},netmask{255,255,255,0},gateway{};
};
/** Boot-static IPv4 UDP echo endpoint, fixed at 192.0.2.2/24, MAC 02:00:00:00:00:02.
 * One instance per boot owns the global NO_SYS stack. All calls, MAC callbacks
 * and timeout servicing belong to the same permanent worker; never call in ISR.
 * RX callbacks copy borrowed frames. TX consumes/copies before returning.
 */
class UdpEcho {
    Ethernet ethernet_{};netif interface_{};udp_pcb* pcb_{};
    uint8_t transmit_[frame_capacity]{};uint8_t payload_[512]{};
    uint32_t echoes_{},errors_{};bool initialized_{};
    static err_t configure(netif* interface);
    static err_t output(netif* interface,pbuf* packet);
    static bool input(void* context,const uint8_t* bytes,size_t length);
    static void received(void* context,udp_pcb* pcb,pbuf* packet,const ip_addr_t* address,u16_t port);
public:
    constexpr UdpEcho()=default;
    UdpEcho(const UdpEcho&)=delete;
    UdpEcho& operator=(const UdpEcho&)=delete;
    /// Claim the single stack instance until reset; port zero and invalid MACs fail.
    bool initialize(Mac mac,uint16_t port=7,const Ipv4Configuration& addressing={});
    /// now_ms is the owner's monotonic millisecond clock, modulo 2^32.
    size_t poll(uint32_t now_ms,size_t budget=4);
    uint32_t echoes()const{return echoes_;}
    uint32_t errors()const{return errors_;}
};
}
