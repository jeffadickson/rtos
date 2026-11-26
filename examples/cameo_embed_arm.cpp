/** @file cameo_embed_arm.cpp
 * @brief Live Cortex-M3 (QEMU MPS2 AN385) NexRT firmware running Cameo.Embed in compact 32-bit
 *        Value mode (sizeof(Value) == 8) with live UART0 MonitorBridge hot-loading, CMSDK Timer0
 *        MMIO inspection, hardware watchpoints, single-stepping, and AXI bus-trace capture.
 *
 * Strictly static at boot: zero heap, zero exceptions, zero RTTI, zero .init_array.
 */
#define CAMEO_EMBED_COMPACT_32 1
#include "rtos_cameo_embed.hpp"

static_assert(sizeof(cameo::embed::Value) == 8u, "Cortex-M3 firmware uses compact 8-byte Value");

namespace {

/// ARM MPS2 AN385 CMSDK APB UART0 register base (mapped to QEMU -serial stdio).
constexpr uintptr_t kUart0Base = 0x40004000u;
constexpr uint32_t kUartStateTxFull = 1u << 0;
constexpr uint32_t kUartStateRxFull = 1u << 1;
constexpr uint32_t kUartCtrlTxEn = 1u << 0;
constexpr uint32_t kUartCtrlRxEn = 1u << 1;

volatile uint32_t& uart0_reg(uint32_t off) noexcept {
    return *reinterpret_cast<volatile uint32_t*>(kUart0Base + off);
}

void uart0_init() noexcept {
    uart0_reg(0x10u) = 16u; // BAUDDIV
    uart0_reg(0x08u) = kUartCtrlTxEn | kUartCtrlRxEn; // CTRL: TX+RX enable
}

void uart0_putc(char c) noexcept {
    while ((uart0_reg(0x04u) & kUartStateTxFull) != 0u) {}
    uart0_reg(0x00u) = static_cast<uint32_t>(static_cast<uint8_t>(c));
}

size_t uart0_write(const char* data, size_t len, void*) noexcept {
    for (size_t i = 0; i < len; ++i) {
        uart0_putc(data[i]);
    }
    return len;
}

void uart0_puts(const char* s) noexcept {
    while (*s != '\0') uart0_putc(*s++);
}

void uart0_hex32(uint32_t v) noexcept {
    static constexpr char kHex[] = "0123456789ABCDEF";
    uart0_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        uart0_putc(kHex[(v >> shift) & 0xFu]);
    }
}

size_t uart0_readline(char* buf, size_t cap) noexcept {
    size_t n = 0;
    for (;;) {
        while ((uart0_reg(0x04u) & kUartStateRxFull) == 0u) {
            rtos::yield();
        }
        const char ch = static_cast<char>(uart0_reg(0x00u) & 0xFFu);
        if (ch == '\r') continue;
        if (ch == '\n') {
            buf[n] = '\0';
            return n;
        }
        if (n + 1u < cap) {
            buf[n++] = ch;
        }
    }
}

/// ARM semihosting exit (QEMU -semihosting-config enable=on,target=native).
[[noreturn]] void semihost_exit(uint32_t status) noexcept {
    const uint32_t block[2]{0x20026u, status};
    register uintptr_t code asm("r0") = 0x20u;
    register const void* arg asm("r1") = block;
    asm volatile("bkpt 0xab" : "+r"(code) : "r"(arg) : "memory", "cc");
    for (;;) asm volatile("wfi");
}

using ArmVm = cameo::embed::StaticVm<
    128, // StackSlots
    48,  // MaxFrames
    16,  // MaxGlobals
    32,  // MaxBoxes
    64,  // MaxClosures
    16,  // MaxRecords
    128, // MaxRecordSlots
    64,  // MaxHostFunctions
    32,  // ScratchWords
    8,   // MaxMemoryWindows
    4,   // MaxWatchpoints
    32,  // BusTraceCapacity
    4    // MaxBreakpoints
>;

static rtos::safe::StaticTask<4096, 1> g_mon_task;
static ArmVm g_vm;
static cameo::nexrt::HostServices g_host;
static cameo::nexrt::MonitorBridge<ArmVm, 12288> g_bridge;
static rtos::trace::TraceRing<32> g_trace;
static rtos::watchdog::Supervisor<4> g_watchdog;
alignas(8) static uint32_t g_scratch_ram[16]{};

bool eq_str(const char* a, const char* b) noexcept {
    while (*a != '\0' && *b != '\0') {
        if (*a++ != *b++) return false;
    }
    return *a == *b;
}

void monitor_entry(void*) {
    uart0_init();

    size_t wdt_id = 0;
    (void)g_watchdog.register_task(wdt_id, 100000u, rtos::ticks());
    g_host.bind_watchdog(g_watchdog);
    g_host.bind_trace(g_trace);
    g_host.install_into(g_vm);

    // Map live Cortex-M3 CMSDK Timer0 (RO, 32-bit aligned) and static RW RAM scratchpad
    (void)g_vm.add_memory_window(cameo::nexrt::AxiWindowPresets::mps2_cmsdk_timer0());
    (void)g_vm.add_memory_window(cameo::nexrt::AxiWindowPresets::ram_window(
        reinterpret_cast<uintptr_t>(g_scratch_ram), sizeof(g_scratch_ram), true));

    g_bridge.attach(g_vm, 50000u);

    rtos::monitor::Hooks hooks{};
    hooks.run_script = &cameo::nexrt::MonitorBridge<ArmVm, 12288>::hook_run_script;
    hooks.context = &g_bridge;
    rtos::monitor::Session session{};
    session.initialize(&uart0_write, nullptr, hooks);

    uart0_puts("CAMEO_EMBED_ARM_READY scratch=");
    uart0_hex32(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch_ram)));
    uart0_puts("\n");

    char line[160]{};
    for (;;) {
        const size_t n = uart0_readline(line, sizeof(line));
        if (n == 0u) continue;
        if (eq_str(line, "quit")) {
            uart0_puts("CAMEO_EMBED_ARM_EXIT\n");
            semihost_exit(0u);
        }
        (void)session.execute_line(line);
    }
}

} // namespace

int main() {
    if (!g_mon_task.register_task(&monitor_entry, nullptr, "cameo_mon")) {
        semihost_exit(1u);
    }
    rtos::start(25000000u);
}
