#include "../examples/static_service_probe.hpp"
#include <cassert>
int main(){
    static nexrt::probe::Wire wire;static rtos::net::UdpEcho echo;
    rtos::net::Ipv4Configuration config;
    config.address={10,0,2,15};config.gateway={10,0,2,2};
    assert(echo.initialize(wire.mac(),7,config));
    // QEMU subnet ARP is checked independently of the default fixture subnet.
    wire.arp();wire.request[28]=10;wire.request[29]=0;wire.request[30]=2;wire.request[31]=2;
    wire.request[38]=10;wire.request[39]=0;wire.request[40]=2;wire.request[41]=15;
    echo.poll(0);assert(wire.response_size>=42);
    assert(wire.response[28]==10&&wire.response[31]==15);
}
