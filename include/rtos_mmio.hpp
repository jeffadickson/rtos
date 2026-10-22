#pragma once
#include <stdint.h>
namespace rtos {
enum class RegisterAccess {read_only,write_only,read_write,write_one_to_clear};
/** @brief BSP policy for uncached ordered device mappings; no barriers implied.
 * A weakly ordered/cacheable BSP must supply another policy with its required
 * fences. Accesses are one volatile word; no atomicity against other callers.
 */
struct DeviceWordIo {
    uint32_t read(uintptr_t address){return *reinterpret_cast<volatile uint32_t*>(address);}
    void write(uintptr_t address,uint32_t value){*reinterpret_cast<volatile uint32_t*>(address)=value;}
};
/** @brief Typed 32-bit register permissions with explicit W1C/no-RMW semantics.
 * Bad alignment and forbidden operations return false before touching the bus.
 * Read failures preserve output. The BSP owns address validity and bus faults.
 * Shared multi-register transactions still need a device ownership lock.
 */
template<RegisterAccess Access,class Io=DeviceWordIo> class Register32 {
    Io& io_;uintptr_t address_;
public:
    constexpr Register32(Io& io,uintptr_t address):io_(io),address_(address){}
    bool read(uint32_t& output){
        if constexpr(Access==RegisterAccess::write_only)return false;
        if(address_&3)return false;
        output=io_.read(address_);return true;
    }
    bool write(uint32_t value){
        if constexpr(Access==RegisterAccess::read_only)return false;
        if(address_&3)return false;
        io_.write(address_,value);return true;
    }
    bool update(uint32_t clear,uint32_t set){
        if constexpr(Access!=RegisterAccess::read_write)return false;
        if(address_&3)return false;
        io_.write(address_,(io_.read(address_)&~clear)|set);return true;
    }
};
}
