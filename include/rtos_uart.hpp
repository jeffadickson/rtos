#pragma once
/** @file
 * @brief Static queued UART transmitter, independent of the processor port.
 * Producers copy complete records; one permanent service thread drains them.
 * Requires RTOS_SYNC and RTOS_LOGGING. No allocation or interrupt-time formatting.
 */
#include "rtos_log.hpp"

namespace rtos {
/** @brief TX-only AXI UARTLite register adapter, with no hardware initialization.
 * The BSP must map base as uncached, ordered device memory and configure clocks,
 * reset and baud. This adapter uses TX+4/status+8, TXFULL bit 3. The simulation
 * endpoint implements this subset; RX, control and UART interrupts are absent.
 * Usable on ARM or RISC-V when the BSP supplies those device-ordering guarantees.
 * Only the queue's permanent service owner may access the adapter directly.
 */
class AxiUartTx {
    uintptr_t base_;
public:
    constexpr explicit AxiUartTx(uintptr_t base):base_(base){}
    /** @brief Offer bytes until TX is full; never spin waiting for room.
     * The return count is consumed synchronously. A full device returns zero.
     * Interrupts stay enabled, allowing normal scheduler preemption during TX.
     */
    int write(const char* text,size_t size){
        auto& status=*reinterpret_cast<volatile uint32_t*>(base_+8);
        auto& tx=*reinterpret_cast<volatile uint32_t*>(base_+4);
        size_t sent=0;
        while(sent<size&&!(status&8u))tx=static_cast<unsigned char>(text[sent++]);
        return static_cast<int>(sent);
    }
};

/** @brief Many thread producers, one UART owner, and copied whole-record output.
 * Transport supplies int write(const char*,size_t), consuming accepted bytes
 * before returning. Queue-full returns Result::full without publishing a partial
 * record. Formatting beyond MessageBytes is reported as truncated; callers must
 * check it. service() preserves the current record across partial writes, so a
 * later producer cannot insert bytes into it. No global printf override exists.
 * The transport must outlive this object; all instances must outlive scheduling.
 */
template<size_t Slots=8,size_t MessageBytes=64,class Transport=AxiUartTx>
class QueuedUart {
    Transport& transport_;
    Logger<Slots,MessageBytes> records_;
    static int transmit(void* self,const char* text,size_t size){
        return static_cast<QueuedUart*>(self)->transport_.write(text,size);
    }
public:
    constexpr explicit QueuedUart(Transport& transport):transport_(transport){}
    QueuedUart(const QueuedUart&)=delete;
    QueuedUart& operator=(const QueuedUart&)=delete;
    /// Copy a record now; the producer may reuse its source after this returns.
    LogResult write(const char* text,size_t size){return records_.write(text,size);}
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf,2,3)))
#endif
    /// Format in private producer storage, then publish as one queue record.
    LogResult printf(const char* format,...){
        va_list arguments;va_start(arguments,format);
        const auto result=records_.vprintf(format,arguments);
        va_end(arguments);return result;
    }
    /** @brief Service at most budget bytes, without waiting for hardware room.
     * Call from one permanent application thread, never an ISR. Logger's drain
     * mutex rejects concurrent/reentrant service without corrupting pending data.
     * A full device returns busy; retry later. Producers never wait for the UART.
     */
    Result service(size_t budget=32){return records_.drain(transmit,this,budget);}
    /// Read queue depth, pending offset and acceptance/drop/error counters.
    LogInfo snapshot()const{return records_.snapshot();}
};
}
