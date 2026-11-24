/** @file examples/storage_demo.cpp
 * @brief Static, zero-heap RISCue RV32IM firmware example exercising:
 *   - RTOS_STORAGE JournalFs over SerialNorAdapter (boot-static RAM-backed SPI bus)
 *   - Drift-free sleep_until() periodic loop + direct-to-task notify()
 *   - StreamBuffer<64> telemetry framing
 *   - Static TimerTable<2> and cooperative watchdog::Supervisor<2> + CrashRecord
 */
#include "rtos.hpp"
#include "rtos_storage.hpp"
#include "rtos_stream.hpp"
#include "rtos_timers.hpp"
#include "rtos_watchdog.hpp"

namespace {

// Board-facing MMIO SPI master register window (external W25Q128JV on SPI0).
struct MmioSpi {
    uintptr_t base{0x10020000u};
    volatile uint32_t& reg(uintptr_t offset) const {
        return *reinterpret_cast<volatile uint32_t*>(base + offset);
    }
};

static MmioSpi g_spi0;
static rtos::storage::SerialNorAdapter g_nor_adapter;
static rtos::storage::JournalFs<8, 8, 256> g_fs;
static rtos::StreamBuffer<64> g_stream;
static rtos::TimerTable<2> g_timers;
static rtos::watchdog::CrashRecord g_crash_record;
static rtos::watchdog::Supervisor<2> g_wdt;

static rtos::Thread g_logger_thread;
static rtos::Thread g_supervisor_thread;
alignas(16) static uint8_t g_logger_stack[1024];
alignas(16) static uint8_t g_supervisor_stack[1024];
static volatile uint32_t g_wdt_pets = 0;
static volatile uint32_t g_timer_ticks = 0;
static size_t g_wdt_logger_id = 0;
static size_t g_wdt_sup_id = 0;

static bool spi_transfer(void* ctx, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len) {
    const auto* spi = static_cast<const MmioSpi*>(ctx);
    if (!spi || (!tx && tx_len) || (!rx && rx_len)) return false;
    spi->reg(0x00u) = 1u; // Assert CS#
    for (size_t i = 0; i < tx_len; ++i) {
        spi->reg(0x04u) = tx[i];
        while ((spi->reg(0x08u) & 1u) != 0u) {}
    }
    for (size_t i = 0; i < rx_len; ++i) {
        spi->reg(0x04u) = 0xFFu;
        while ((spi->reg(0x08u) & 1u) != 0u) {}
        rx[i] = static_cast<uint8_t>(spi->reg(0x0Cu) & 0xFFu);
    }
    spi->reg(0x00u) = 0u; // Deassert CS#
    return true;
}

static void pet_hw_wdt(void*) { g_wdt_pets = g_wdt_pets + 1u; }
static void timer_cb(void*) { g_timer_ticks = g_timer_ticks + 1u; }

static void logger_entry(void*) {
    (void)g_nor_adapter.initialize({&g_spi0, spi_transfer, 1000u}, 8u, 16u, 256u);
    (void)g_fs.mount(g_nor_adapter.backend());

    rtos::Tick next_wake = rtos::ticks();
    uint32_t seq = 0;
    for (;;) {
        ++seq;
        (void)g_fs.write_file(0, &seq, sizeof(seq));
        size_t written = 0;
        (void)g_stream.send(&seq, sizeof(seq), written);
        (void)rtos::notify(g_supervisor_thread, 0x01u, rtos::NotifyAction::set_bits);
        (void)g_wdt.checkin(g_wdt_logger_id, rtos::ticks());
        rtos::sleep_until(next_wake, 10u);
    }
}

static void supervisor_entry(void*) {
    size_t t_handle = 0;
    (void)g_timers.create(t_handle, timer_cb, nullptr, 20u, true);
    (void)g_timers.start(t_handle, rtos::ticks());

    for (;;) {
        uint32_t bits = 0;
        (void)rtos::wait_notification(bits, 0xFFFFFFFFu, 10u);
        uint32_t sample = 0;
        size_t got = 0;
        (void)g_stream.receive(&sample, sizeof(sample), got);
        const rtos::Tick now = rtos::ticks();
        (void)g_timers.tick(now);
        (void)g_wdt.checkin(g_wdt_sup_id, now);
        (void)g_wdt.evaluate(now);
    }
}

} // namespace

int main() {
    (void)g_wdt.initialize(pet_hw_wdt, nullptr, &g_crash_record);
    (void)g_wdt.register_task(g_wdt_logger_id, 50u, 0u);
    (void)g_wdt.register_task(g_wdt_sup_id, 50u, 0u);
    (void)rtos::create(g_logger_thread, logger_entry, nullptr, 3, g_logger_stack, sizeof(g_logger_stack), "storage");
    (void)rtos::create(g_supervisor_thread, supervisor_entry, nullptr, 4, g_supervisor_stack, sizeof(g_supervisor_stack), "super");
    rtos::start(50000000u);
}
