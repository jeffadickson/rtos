/** @file tests/cameo_embed.cpp
 * @brief End-to-end NexRT integration test for Cameo.Embed (RTOS_CAMEO_EMBED):
 *   - Executes a real Cameo.Embed-compiled CEMB v1 hardware diagnostic script (axi_diag.cameo)
 *     embedded in Flash .rodata via tests/scripts/cameo_axi_diag_image.hpp
 *   - Verifies first-class Ptr arithmetic, alignment checks, and AXI4-Lite/SRAM MMIO windows
 *   - Verifies board MemoryWindow policy enforcement (read-only AXI write denial, width/align faults)
 *   - Verifies dbg.hexdump, dbg.log_hex, dbg.bits, dbg.assert, dbg.trace -> rtos::trace::TraceRing
 *   - Verifies rtos.task_count / rtos.task_guard_ok / rtos.watchdog_checkin -> rtos::watchdog::Supervisor
 *   - Verifies zero-heap live UART hot-loading via rtos::monitor::Session + cameo::nexrt::MonitorBridge
 *     (script reset / script hex / script load / script run / script status + 1-bit CRC rejection)
 */
#include "rtos_cameo_embed.hpp"
#include "scripts/cameo_axi_diag_image.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

rtos::Tick g_ticks = 100u;
uint32_t g_yields = 0u;

struct MockSoC {
    uint8_t uart_regs[256]{};
    uint8_t hwsched_regs[1024]{};
    uint8_t sram[512]{};
    uint32_t fences{0};

    void seed32(uint64_t addr, uint32_t val) noexcept {
        (void)write_cb(this, addr, 4u, val);
    }

    uint32_t peek32(uint64_t addr) const noexcept {
        uint64_t out = 0;
        (void)read_cb(const_cast<MockSoC*>(this), addr, 4u, out);
        return static_cast<uint32_t>(out);
    }

    static bool read_cb(void* ctx, uint64_t addr, uint8_t width, uint64_t& out) noexcept {
        auto* self = static_cast<MockSoC*>(ctx);
        const uint8_t* ptr = nullptr;
        if (addr >= 0x10000000ULL && addr + width <= 0x10000100ULL)
            ptr = self->uart_regs + (addr - 0x10000000ULL);
        else if (addr >= 0x10010000ULL && addr + width <= 0x10010400ULL)
            ptr = self->hwsched_regs + (addr - 0x10010000ULL);
        else if (addr >= 0x20000000ULL && addr + width <= 0x20000200ULL)
            ptr = self->sram + (addr - 0x20000000ULL);
        else
            return false;

        uint64_t v = 0;
        for (uint8_t i = 0; i < width; ++i)
            v |= static_cast<uint64_t>(ptr[i]) << (i * 8u);
        out = v;
        return true;
    }

    static bool write_cb(void* ctx, uint64_t addr, uint8_t width, uint64_t val) noexcept {
        auto* self = static_cast<MockSoC*>(ctx);
        uint8_t* ptr = nullptr;
        if (addr >= 0x10000000ULL && addr + width <= 0x10000100ULL)
            ptr = self->uart_regs + (addr - 0x10000000ULL);
        else if (addr >= 0x10010000ULL && addr + width <= 0x10010400ULL)
            ptr = self->hwsched_regs + (addr - 0x10010000ULL);
        else if (addr >= 0x20000000ULL && addr + width <= 0x20000200ULL)
            ptr = self->sram + (addr - 0x20000000ULL);
        else
            return false;

        for (uint8_t i = 0; i < width; ++i)
            ptr[i] = static_cast<uint8_t>((val >> (i * 8u)) & 0xFFu);
        return true;
    }

    static void fence_cb(void* ctx) noexcept {
        ++static_cast<MockSoC*>(ctx)->fences;
    }
};

struct FakeKernelTasks {
    rtos::ThreadInfo rows[2]{};
    size_t count{2};

    static rtos::SystemInfo snapshot_cb(void* ctx, rtos::ThreadInfo* out, size_t cap) noexcept {
        auto* self = static_cast<FakeKernelTasks*>(ctx);
        rtos::SystemInfo info{};
        info.ticks = g_ticks;
        info.required_threads = self->count;
        if (out != nullptr && cap > 0u) {
            const size_t n = self->count < cap ? self->count : cap;
            for (size_t i = 0; i < n; ++i) out[i] = self->rows[i];
            info.written_threads = n;
        }
        return info;
    }
};

struct TextCapture {
    std::string text;
    static bool write_log(void* ctx, const char* data, size_t len) noexcept {
        static_cast<TextCapture*>(ctx)->text.append(data, len);
        return true;
    }
    static size_t write_monitor(const char* data, size_t len, void* ctx) {
        static_cast<TextCapture*>(ctx)->text.append(data, len);
        return len;
    }
};

} // namespace

// Minimal host stubs for kernel functions referenced by cameo_embed_nexrt.hpp
namespace rtos {
Tick ticks() { return g_ticks; }
bool sleep_for(Tick dt) { g_ticks += dt; return true; }
void yield() { ++g_yields; }
SystemInfo snapshot(ThreadInfo*, size_t) { return {}; }
} // namespace rtos

namespace {

using Req = nexrt::tests::cameo_diag::kAxiDiagImage_requirements;
using DiagVm = cameo::embed::StaticVm<
    Req::stack_slots, Req::frames, Req::globals,
    Req::boxes, Req::closures, Req::records, Req::record_slots,
    48, 32, 8>;

static_assert(!Req::uses_float, "Default Cameo.Embed image must be integer-only (zero float/libm)");

void configure_soc(MockSoC& soc, DiagVm& vm) {
    soc.seed32(0x10000008ULL, 0x00000004u); // UART TX empty, not full
    soc.seed32(0x10010004ULL, 0x00000003u); // HWSched active_tid=3, seu_fault=0
    soc.seed32(0x20000000ULL, 0x11223344u);
    soc.seed32(0x20000004ULL, 0x55667788u);
    soc.seed32(0x20000008ULL, 0xAABBCCDDu);
    soc.seed32(0x2000000CULL, 0x13579BDFu);

    vm.set_bus_backend({&soc, &MockSoC::read_cb, &MockSoC::write_cb, &MockSoC::fence_cb});
    assert(vm.add_memory_window(cameo::nexrt::AxiWindowPresets::riscue_uartlite(0x10000000ULL, true)));
    assert(vm.add_memory_window(cameo::nexrt::AxiWindowPresets::nexrt_hwsched(0x10010000ULL, false)));
    assert(vm.add_memory_window(cameo::nexrt::AxiWindowPresets::ram_window(0x20000000ULL, 0x200u, true)));
}

void test_flash_rodata_axi_diagnostics() {
    static DiagVm vm;
    MockSoC soc{};
    configure_soc(soc, vm);

    FakeKernelTasks tasks{};
    tasks.rows[0] = {nullptr, "ctrl", rtos::State::ready, 3, 3, true, false, true, 2048, 12000, 10};
    tasks.rows[1] = {nullptr, "idle", rtos::State::ready, 0, 0, false, true, true, 1024, 5000, 4};

    rtos::watchdog::Supervisor<4> wdt;
    size_t wdt_id = 99u;
    assert(wdt.register_task(wdt_id, 50u, g_ticks) && wdt_id == 0u);
    rtos::trace::TraceRing<16> trace_ring;
    TextCapture logs{};

    cameo::nexrt::HostServices host{};
    host.log_ctx = &logs;
    host.log_write = &TextCapture::write_log;
    host.bind_watchdog(wdt);
    host.bind_trace(trace_ring);
    host.bind_snapshot(&FakeKernelTasks::snapshot_cb, &tasks);
    host.install_into(vm);

    // Load compiled CEMB v1 image directly from Flash .rodata
    assert(vm.load(nexrt::tests::cameo_diag::kAxiDiagImage) == cameo::embed::Status::Ok);
    assert(vm.image_crc32c() == Req::crc32c);
    auto init_res = vm.run_main(50000u);
    assert(init_res.status == cameo::embed::Status::Ok);

    // Execute diagnose_bus(7) and check AXI RMW, burst XOR checksum, hexdump, trace, and watchdog!
    const cameo::embed::Value args[1] = {cameo::embed::Value::from_int(7)};
    cameo::embed::Value out[1]{};
    auto res = vm.call_export("diagnose_bus", args, out, 50000u);
    assert(res.status == cameo::embed::Status::Ok);
    assert(res.result_count == 1u && out[0].is_int());

    const int64_t expected_xor = static_cast<int64_t>(0x11223344u ^ 0x55667788u ^ 0xAABBCCDDu ^ 0x13579BDFu);
    assert(out[0].as_int() == expected_xor);
    assert(soc.peek32(0x1000000CULL) == 0x0000005Au);
    assert(soc.fences == 1u);
    assert(trace_ring.count() == 1u);
    rtos::trace::Event ev{};
    assert(trace_ring.read_oldest(0u, ev));
    assert(ev.event_id == 0x0100u);
    assert(ev.arg == ((7u << 16) | (static_cast<uint32_t>(expected_xor) & 0xFFFFu)));
    assert(logs.text.find("uart_stat=0x00000004") != std::string::npos);
    assert(logs.text.find("0x20000000: 44 33 22 11 88 77 66 55 DD CC BB AA DF 9B 57 13") != std::string::npos);
    assert(logs.text.find("active_tid=3") != std::string::npos);

    // Inject a stack-guard fault on task 1 and verify the script catches and reports code 3!
    tasks.rows[1].guard_ok = false;
    auto fault_res = vm.call_export("diagnose_bus", args, out, 50000u);
    assert(fault_res.status == cameo::embed::Status::Ok);
    assert(out[0].as_int() == 3); // HealthCode::stack_guard_fault
    assert(trace_ring.count() == 2u);

    // Inject HWSched SEU fault bit (bit 8) and verify script catches and reports code 2!
    tasks.rows[1].guard_ok = true;
    soc.seed32(0x10010004ULL, 0x00000103u);
    auto seu_res = vm.call_export("diagnose_bus", args, out, 50000u);
    assert(seu_res.status == cameo::embed::Status::Ok);
    assert(out[0].as_int() == 2); // HealthCode::hwsched_seu_fault
}

void test_monitor_uart_hot_loading() {
    static DiagVm vm;
    static cameo::nexrt::MonitorBridge<DiagVm, 8192> bridge;
    MockSoC soc{};
    configure_soc(soc, vm);

    FakeKernelTasks tasks{};
    tasks.rows[0] = {nullptr, "ctrl", rtos::State::ready, 3, 3, true, false, true, 2048, 12000, 10};
    tasks.count = 1u;

    TextCapture logs{};
    cameo::nexrt::HostServices host{};
    host.log_ctx = &logs;
    host.log_write = &TextCapture::write_log;
    host.bind_snapshot(&FakeKernelTasks::snapshot_cb, &tasks);
    host.install_into(vm);
    bridge.attach(vm, 50000u);

    TextCapture mon_out{};
    rtos::monitor::Session mon{};
    rtos::monitor::Hooks hooks{};
    hooks.run_script = &cameo::nexrt::MonitorBridge<DiagVm, 8192>::hook_run_script;
    hooks.context = &bridge;
    mon.initialize(&TextCapture::write_monitor, &mon_out, hooks);

    assert(mon.execute_line("script reset"));
    assert(mon_out.text.find("ok reset") != std::string::npos);

    // Stream the full CEMB v1 image in 40-byte hex chunks over the monitor command line
    const auto img = std::span<const uint8_t>(nexrt::tests::cameo_diag::kAxiDiagImage);
    for (size_t off = 0; off < img.size(); off += 40u) {
        const size_t n = (off + 40u <= img.size()) ? 40u : (img.size() - off);
        std::string cmd = "script hex ";
        for (size_t j = 0; j < n; ++j) {
            char pair[3]{};
            cameo::embed::detail::write_hex_digits(pair, img[off + j], 2u);
            cmd.append(pair, 2u);
        }
        assert(mon.execute_line(cmd.c_str()));
    }

    mon_out.text.clear();
    assert(mon.execute_line("script load"));
    assert(mon_out.text.find("ok loaded crc=0x") != std::string::npos);

    mon_out.text.clear();
    assert(mon.execute_line("script run diagnose_bus 9"));
    const int64_t expected_xor = static_cast<int64_t>(0x11223344u ^ 0x55667788u ^ 0xAABBCCDDu ^ 0x13579BDFu);
    assert(mon_out.text.find("ok ret=" + std::to_string(expected_xor)) != std::string::npos);

    mon_out.text.clear();
    assert(mon.execute_line("script status"));
    assert(mon_out.text.find("loaded=1") != std::string::npos);

    // Corrupted 1-bit upload must fail verify with ChecksumMismatch (3)
    assert(mon.execute_line("script reset"));
    for (size_t off = 0; off < img.size(); off += 40u) {
        const size_t n = (off + 40u <= img.size()) ? 40u : (img.size() - off);
        std::string cmd = "script hex ";
        for (size_t j = 0; j < n; ++j) {
            uint8_t b = img[off + j];
            if (off + j == img.size() - 1u) b ^= 0x01u;
            char pair[3]{};
            cameo::embed::detail::write_hex_digits(pair, b, 2u);
            cmd.append(pair, 2u);
        }
        assert(mon.execute_line(cmd.c_str()));
    }
    mon_out.text.clear();
    assert(mon.execute_line("script load"));
    assert(mon_out.text.find("err verify=3") != std::string::npos);
    assert(bridge.last_status() == cameo::embed::Status::ChecksumMismatch);
}

} // namespace

int main() {
    test_flash_rodata_axi_diagnostics();
    test_monitor_uart_hot_loading();
    std::printf("cameo_embed NexRT integration tests passed (image=%zu bytes, crc=0x%08X)\n",
                Req::image_bytes, Req::crc32c);
    return 0;
}
