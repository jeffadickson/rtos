/** @file tests/flash_axi_verilator.cpp
 * @brief End-to-end Verilator Hardware/Software Co-Simulation proving NexRT's
 *        RTOS_STORAGE JournalFs and protocol adapters running live over real
 *        AXI4-Lite SystemVerilog RTL controllers for ALL FOUR flash families:
 *          1. Serial SPI NOR   (CS0# -> nexrt_spi_flash_axi -> SerialNorMock)
 *          2. Serial SPI NAND  (CS1# -> nexrt_spi_flash_axi -> SerialNandMock)
 *          3. Parallel ONFI x8 (CLE/ALE/WE#/RE#/DQ/R/B# -> nexrt_onfi_nand_axi -> ParallelNandMock)
 *             + bit-exact verification of Verilog HW 22-bit Hamming-256 ECC engine
 *          4. Parallel 16-bit CFI NOR (CE#/OE#/WE#/ADDR/DQ -> nexrt_cfi_nor_axi -> ParallelNorMock)
 */
#include "Vnexrt_flash_subsys_axi.h"
#include "rtos_flash_mocks.hpp"
#include "rtos_storage.hpp"
#include "rtos_storage_axi.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace rtos::storage;
using namespace rtos::storage::axi;
using namespace rtos::storage::mocks;

namespace {

// =============================================================================
// Pin-Level Behavioral Flash Transceivers Wired to Verilated RTL External Pins
// =============================================================================

/// Pin-level SPI slave transceiver bridging physical (SCLK, MOSI, MISO, CS#)
/// to our SerialNorMock / SerialNandMock SpiBus protocol models (supports SPI Modes 0..3).
class SpiPinTransceiver {
    SpiBus target_{};
    bool cpol_{false};
    bool cpha_{false};
    bool prev_cs_n_{true};
    bool prev_sclk_{false};
    bool prev_mosi_{false};
    uint8_t rx_byte_{};
    uint8_t rx_bits_{};
    std::vector<uint8_t> tx_frame_;
    std::vector<uint8_t> resp_bytes_;
    size_t resp_idx_{};
    uint8_t resp_bit_{};
    bool serving_response_{};
    bool resp_armed_{};

    /// Determine how many command/address/dummy bytes precede read data for a SPI opcode.
    static size_t read_header_len(const std::vector<uint8_t>& frame, bool is_nand) {
        if (frame.empty()) return 0;
        const uint8_t op = frame[0];
        if (!is_nand) {
            if (op == 0x9Fu) return 1; // JEDEC ID -> 3 bytes read
            if (op == 0x05u) return 1; // Read Status Register -> 1 byte read
            if (op == 0x03u) return 4; // Read Data (op + 24-bit addr) -> N bytes read
            return 0;
        } else {
            if (op == 0x9Fu) return 2; // JEDEC ID (op + dummy) -> 2-3 bytes read
            if (op == 0x0Fu) return 2; // Get Feature (op + reg) -> 1 byte read
            if (op == 0x03u) return 4; // Read From Cache (op + 16-bit col + dummy) -> N bytes read
            return 0;
        }
    }

    bool is_nand_{false};

public:
    void set_mode(uint8_t mode) {
        cpol_ = (mode & 1u) != 0u;
        cpha_ = (mode & 2u) != 0u;
    }

    void attach(const SpiBus& bus, bool is_nand) {
        target_ = bus;
        is_nand_ = is_nand;
        cpol_ = false;
        cpha_ = false;
        prev_cs_n_ = true;
        prev_sclk_ = false;
        prev_mosi_ = false;
        tx_frame_.clear();
        resp_bytes_.clear();
        serving_response_ = false;
        resp_armed_ = false;
    }

    uint8_t step(bool cs_n, bool sclk, bool mosi) {
        if (cs_n) {
            // CS# rising edge: commit pure-TX frame (WREN, Page Program, Erase, Load, Execute)
            if (!prev_cs_n_ && !tx_frame_.empty() && !serving_response_) {
                (void)target_.transfer(target_.context, tx_frame_.data(), tx_frame_.size(), nullptr, 0);
            }
            tx_frame_.clear();
            resp_bytes_.clear();
            rx_byte_ = 0;
            rx_bits_ = 0;
            serving_response_ = false;
            resp_armed_ = false;
            prev_cs_n_ = true;
            prev_sclk_ = sclk;
            prev_mosi_ = mosi;
            return 1u;
        }

        if (prev_cs_n_) {
            // CS# falling edge: start new SPI frame
            tx_frame_.clear();
            resp_bytes_.clear();
            rx_byte_ = 0;
            rx_bits_ = 0;
            resp_idx_ = 0;
            resp_bit_ = 0;
            serving_response_ = false;
            resp_armed_ = false;
            prev_cs_n_ = false;
            prev_sclk_ = sclk;
            prev_mosi_ = mosi;
        }

        const bool leading_edge  = (sclk != cpol_) && (prev_sclk_ == cpol_);
        const bool trailing_edge = (sclk == cpol_) && (prev_sclk_ != cpol_);
        const bool sample_edge   = cpha_ ? trailing_edge : leading_edge;
        const bool shift_edge    = cpha_ ? leading_edge  : trailing_edge;

        if (sample_edge) {
            // Regression check for Bug #3 (SPI CPHA=1 MOSI hold-time race):
            // MOSI must remain stable across every sample edge (zero same-edge transition).
            assert(mosi == prev_mosi_ && "SPI MOSI changed on sample edge (hold-time violation)!");
            rx_byte_ = static_cast<uint8_t>((rx_byte_ << 1) | (mosi ? 1u : 0u));
            ++rx_bits_;
            if (rx_bits_ == 8u) {
                if (!serving_response_) {
                    tx_frame_.push_back(rx_byte_);
                    const size_t hdr = read_header_len(tx_frame_, is_nand_);
                    if (hdr > 0 && tx_frame_.size() == hdr) {
                        size_t rx_n = 1;
                        if (tx_frame_[0] == 0x9Fu) rx_n = 3;
                        else if (tx_frame_[0] == 0x05u || tx_frame_[0] == 0x0Fu) rx_n = 1;
                        else if (tx_frame_[0] == 0x03u) {
                            if (is_nand_) {
                                const size_t col = (static_cast<size_t>(tx_frame_[1]) << 8) | tx_frame_[2];
                                rx_n = col < 288u ? (288u - col) : 1u;
                            } else {
                                rx_n = 256u;
                            }
                        }
                        resp_bytes_.assign(rx_n, 0xFFu);
                        (void)target_.transfer(target_.context, tx_frame_.data(), tx_frame_.size(),
                                               resp_bytes_.data(), resp_bytes_.size());
                        serving_response_ = true;
                        resp_armed_ = false;
                        resp_idx_ = 0;
                        resp_bit_ = 0;
                    }
                }
                rx_byte_ = 0;
                rx_bits_ = 0;
            }
        } else if (shift_edge) {
            if (serving_response_) {
                if (!resp_armed_) {
                    resp_armed_ = true;
                } else {
                    ++resp_bit_;
                    if (resp_bit_ == 8u) {
                        resp_bit_ = 0;
                        ++resp_idx_;
                    }
                }
            }
        }

        prev_sclk_ = sclk;
        prev_mosi_ = mosi;
        if (serving_response_ && resp_armed_ && resp_idx_ < resp_bytes_.size()) {
            return static_cast<uint8_t>((resp_bytes_[resp_idx_] >> (7u - resp_bit_)) & 1u);
        }
        return 1u;
    }
};

/// Pin-level ONFI x8 Parallel NAND transceiver wired to (CE#, CLE, ALE, WE#, RE#, DQ, R/B#).
class OnfiPinTransceiver {
    ParallelNandBus target_{};
    bool prev_we_n_{true};
    bool prev_re_n_{true};
    uint8_t dq_in_{0xFFu};

public:
    void attach(const ParallelNandBus& bus) {
        target_ = bus;
        prev_we_n_ = true;
        prev_re_n_ = true;
        dq_in_ = 0xFFu;
    }

    void step(bool ce_n, bool cle, bool ale, bool we_n, bool re_n,
              uint8_t dq_out, bool dq_oe, uint8_t& out_dq_in, uint8_t& out_rb_n) {
        if (!ce_n) {
            // WE# rising edge latches command, address, or write data
            if (we_n && !prev_we_n_ && dq_oe) {
                if (cle) target_.write_cmd(target_.context, dq_out);
                else if (ale) target_.write_addr(target_.context, dq_out);
                else target_.write_data(target_.context, &dq_out, 1u);
            }
            // RE# falling edge outputs the next byte onto DQ[7:0]
            if (!re_n && prev_re_n_) {
                uint8_t b = 0xFFu;
                target_.read_data(target_.context, &b, 1u);
                dq_in_ = b;
            }
        }
        prev_we_n_ = we_n;
        prev_re_n_ = re_n;
        out_dq_in = dq_in_;
        out_rb_n = target_.is_ready(target_.context) ? 1u : 0u;
    }
};

/// Pin-level 16-bit CFI Parallel NOR transceiver wired to (CE#, OE#, WE#, ADDR, DQ[15:0], RY/BY#).
class CfiPinTransceiver {
    ParallelNorBus target_{};
    bool prev_we_n_{true};
    bool prev_oe_n_{true};
    uint16_t dq_in_{0xFFFFu};

public:
    void attach(const ParallelNorBus& bus) {
        target_ = bus;
        prev_we_n_ = true;
        prev_oe_n_ = true;
        dq_in_ = 0xFFFFu;
    }

    void step(bool ce_n, bool oe_n, bool we_n, uint32_t addr,
              uint16_t dq_out, bool dq_oe, uint16_t& out_dq_in, uint8_t& out_ry_by_n) {
        if (!ce_n) {
            if (we_n && !prev_we_n_ && dq_oe) {
                target_.write16(target_.context, addr, dq_out);
            }
            if (!oe_n && prev_oe_n_) {
                dq_in_ = target_.read16(target_.context, addr);
            }
        }
        prev_we_n_ = we_n;
        prev_oe_n_ = oe_n;
        out_dq_in = dq_in_;
        out_ry_by_n = 1u;
    }
};

// =============================================================================
// Full Verilator SoC Testbench Harness Driving Real AXI4-Lite Handshakes
// =============================================================================
struct FlashSocHarness {
    Vnexrt_flash_subsys_axi dut;
    SerialNorMock<8, 4, 256> nor_chip;
    SerialNandMock<8, 4, 256, 32> snand_chip;
    ParallelNandMock<8, 4, 256, 32> pnand_chip;
    ParallelNorMock<8, 4, 256> pnor_chip;

    SpiPinTransceiver spi0_nor;
    SpiPinTransceiver spi1_nand;
    OnfiPinTransceiver onfi_nand;
    CfiPinTransceiver cfi_nor;
    uint64_t cycles{0};

    void init() {
        nor_chip.reset_erased();
        snand_chip.reset_erased();
        pnand_chip.reset_erased();
        pnor_chip.reset_erased();

        spi0_nor.attach(nor_chip.spi_bus(), false);
        spi1_nand.attach(snand_chip.spi_bus(), true);
        onfi_nand.attach(pnand_chip.bus());
        cfi_nor.attach(pnor_chip.bus());

        dut.clk = 0;
        dut.rst_n = 0;
        dut.s_axi_awaddr = 0;
        dut.s_axi_awvalid = 0;
        dut.s_axi_wdata = 0;
        dut.s_axi_wstrb = 0xFu;
        dut.s_axi_wvalid = 0;
        dut.s_axi_bready = 0;
        dut.s_axi_araddr = 0;
        dut.s_axi_arvalid = 0;
        dut.s_axi_rready = 0;
        dut.spi_miso = 1;
        dut.nand_dq_in = 0xFFu;
        dut.nand_rb_n = 1;
        dut.nor_dq_in = 0xFFFFu;
        dut.nor_ry_by_n = 1;

        for (int i = 0; i < 4; ++i) tick();
        dut.rst_n = 1;
        for (int i = 0; i < 2; ++i) tick();
    }

    void update_pins() {
        const uint8_t cs_n = dut.spi_cs_n;
        const uint8_t miso0 = spi0_nor.step((cs_n & 1u) != 0u, dut.spi_sclk != 0u, dut.spi_mosi != 0u);
        const uint8_t miso1 = spi1_nand.step((cs_n & 2u) != 0u, dut.spi_sclk != 0u, dut.spi_mosi != 0u);
        dut.spi_miso = ((cs_n & 1u) == 0u) ? miso0 : (((cs_n & 2u) == 0u) ? miso1 : 1u);

        uint8_t ndq = 0xFFu, nrb = 1u;
        onfi_nand.step(dut.nand_ce_n != 0u, dut.nand_cle != 0u, dut.nand_ale != 0u,
                       dut.nand_we_n != 0u, dut.nand_re_n != 0u,
                       dut.nand_dq_out, dut.nand_dq_oe != 0u, ndq, nrb);
        dut.nand_dq_in = ndq;
        dut.nand_rb_n = nrb;

        uint16_t rdq = 0xFFFFu;
        uint8_t rry = 1u;
        cfi_nor.step(dut.nor_ce_n != 0u, dut.nor_oe_n != 0u, dut.nor_we_n != 0u,
                     dut.nor_addr, dut.nor_dq_out, dut.nor_dq_oe != 0u, rdq, rry);
        dut.nor_dq_in = rdq;
        dut.nor_ry_by_n = rry;
    }

    void tick() {
        dut.clk = 0;
        dut.eval();
        update_pins();
        dut.eval();
        dut.clk = 1;
        dut.eval();
        update_pins();
        dut.eval();
        ++cycles;
    }

    /// Standard simultaneous AWVALID + WVALID AXI4-Lite write.
    uint8_t axi_write(uint32_t addr, uint32_t data, uint8_t strb = 0xFu) {
        dut.s_axi_awaddr = addr;
        dut.s_axi_awvalid = 1;
        dut.s_axi_wdata = data;
        dut.s_axi_wstrb = strb;
        dut.s_axi_wvalid = 1;
        dut.s_axi_bready = 1;

        bool aw_done = false, w_done = false;
        for (int guard = 0; guard < 32; ++guard) {
            dut.eval();
            const bool aw_h = !aw_done && dut.s_axi_awvalid && dut.s_axi_awready;
            const bool w_h  = !w_done  && dut.s_axi_wvalid  && dut.s_axi_wready;
            tick();
            if (aw_h) { aw_done = true; dut.s_axi_awvalid = 0; }
            if (w_h)  { w_done  = true; dut.s_axi_wvalid  = 0; }
            if (aw_done && w_done && dut.s_axi_bvalid) {
                const uint8_t resp = dut.s_axi_bresp;
                tick();
                dut.s_axi_bready = 0;
                return resp;
            }
        }
        assert(false && "AXI4-Lite write timed out");
        return 2u;
    }

    /// Regression test helper for Bug #2: WVALID arrives BEFORE AWVALID (legal AMBA AXI4-Lite).
    uint8_t axi_write_w_before_aw(uint32_t addr, uint32_t data, uint8_t strb = 0xFu) {
        dut.s_axi_awvalid = 0;
        dut.s_axi_wdata = data;
        dut.s_axi_wstrb = strb;
        dut.s_axi_wvalid = 1;
        dut.s_axi_bready = 1;

        // W channel MUST be accepted without waiting for AWVALID!
        bool w_done = false;
        for (int i = 0; i < 4; ++i) {
            dut.eval();
            const bool w_h = !w_done && dut.s_axi_wvalid && dut.s_axi_wready;
            tick();
            if (w_h) { w_done = true; dut.s_axi_wvalid = 0; break; }
        }
        assert(w_done && "AXI4-Lite deadlock: WREADY waited on AWVALID!");
        tick(); // Extra idle cycle between W and AW

        dut.s_axi_awaddr = addr;
        dut.s_axi_awvalid = 1;
        bool aw_done = false;
        for (int guard = 0; guard < 32; ++guard) {
            dut.eval();
            const bool aw_h = !aw_done && dut.s_axi_awvalid && dut.s_axi_awready;
            tick();
            if (aw_h) { aw_done = true; dut.s_axi_awvalid = 0; }
            if (aw_done && dut.s_axi_bvalid) {
                const uint8_t resp = dut.s_axi_bresp;
                tick();
                dut.s_axi_bready = 0;
                return resp;
            }
        }
        assert(false && "AXI4-Lite W-before-AW write timed out");
        return 2u;
    }

    /// Regression test helper for Bug #2: AWVALID arrives BEFORE WVALID.
    uint8_t axi_write_aw_before_w(uint32_t addr, uint32_t data, uint8_t strb = 0xFu) {
        dut.s_axi_wvalid = 0;
        dut.s_axi_awaddr = addr;
        dut.s_axi_awvalid = 1;
        dut.s_axi_bready = 1;

        bool aw_done = false;
        for (int i = 0; i < 4; ++i) {
            dut.eval();
            const bool aw_h = !aw_done && dut.s_axi_awvalid && dut.s_axi_awready;
            tick();
            if (aw_h) { aw_done = true; dut.s_axi_awvalid = 0; break; }
        }
        assert(aw_done && "AXI4-Lite deadlock: AWREADY waited on WVALID!");
        tick();

        dut.s_axi_wdata = data;
        dut.s_axi_wstrb = strb;
        dut.s_axi_wvalid = 1;
        bool w_done = false;
        for (int guard = 0; guard < 32; ++guard) {
            dut.eval();
            const bool w_h = !w_done && dut.s_axi_wvalid && dut.s_axi_wready;
            tick();
            if (w_h) { w_done = true; dut.s_axi_wvalid = 0; }
            if (w_done && dut.s_axi_bvalid) {
                const uint8_t resp = dut.s_axi_bresp;
                tick();
                dut.s_axi_bready = 0;
                return resp;
            }
        }
        assert(false && "AXI4-Lite AW-before-W write timed out");
        return 2u;
    }

    uint32_t axi_read(uint32_t addr, uint8_t* out_resp = nullptr) {
        dut.s_axi_araddr = addr;
        dut.s_axi_arvalid = 1;
        dut.s_axi_rready = 1;

        bool ar_done = false;
        for (int guard = 0; guard < 32; ++guard) {
            dut.eval();
            const bool ar_h = !ar_done && dut.s_axi_arvalid && dut.s_axi_arready;
            tick();
            if (ar_h) { ar_done = true; dut.s_axi_arvalid = 0; }
            if (ar_done && dut.s_axi_rvalid) {
                const uint32_t val = dut.s_axi_rdata;
                if (out_resp) *out_resp = dut.s_axi_rresp;
                tick();
                dut.s_axi_rready = 0;
                return val;
            }
        }
        assert(false && "AXI4-Lite read timed out");
        return 0u;
    }

    static void bus_wr(void* ctx, uint32_t addr, uint32_t val) {
        const uint8_t resp = static_cast<FlashSocHarness*>(ctx)->axi_write(addr, val);
        assert(resp == 0u);
    }

    static uint32_t bus_rd(void* ctx, uint32_t addr) {
        uint8_t resp = 0;
        const uint32_t v = static_cast<FlashSocHarness*>(ctx)->axi_read(addr, &resp);
        assert(resp == 0u);
        return v;
    }

    AxiBus axi_bus() { return AxiBus{this, &bus_wr, &bus_rd}; }
};

void verify_fs_roundtrip(const char* label, FlashBackend backend) {
    JournalFs<8, 4, 256> fs;
    assert(fs.mount(backend) == FsResult::ok);

    const char msg1[] = "NEXRT-AXI-FLASH-VERILATOR-OK";
    const char msg2[] = "UPDATED-RECORD-THROUGH-RTL!";
    assert(fs.write_file(0, msg1, sizeof(msg1)) == FsResult::ok);
    assert(fs.write_file(1, msg2, sizeof(msg2)) == FsResult::ok);

    char rd[64]{};
    size_t out_len = 0;
    assert(fs.read_file(0, rd, sizeof(rd), out_len) == FsResult::ok);
    assert(out_len == sizeof(msg1) && std::memcmp(rd, msg1, sizeof(msg1)) == 0);

    // Overwrite file 0 repeatedly to exercise block rollover and garbage collection over RTL pins
    for (uint32_t i = 0; i < 32; ++i) {
        char payload[48]{};
        std::snprintf(payload, sizeof(payload), "%s-ITER-%02u", label, i);
        assert(fs.write_file(0, payload, std::strlen(payload) + 1) == FsResult::ok);
    }

    // Remount fresh JournalFs instance to prove cold-boot recovery over AXI4-Lite RTL!
    JournalFs<8, 4, 256> fs_reboot;
    assert(fs_reboot.mount(backend) == FsResult::ok);
    std::memset(rd, 0, sizeof(rd));
    assert(fs_reboot.read_file(0, rd, sizeof(rd), out_len) == FsResult::ok);
    char expected[48]{};
    std::snprintf(expected, sizeof(expected), "%s-ITER-31", label);
    assert(std::strcmp(rd, expected) == 0);

    std::memset(rd, 0, sizeof(rd));
    assert(fs_reboot.read_file(1, rd, sizeof(rd), out_len) == FsResult::ok);
    assert(std::memcmp(rd, msg2, sizeof(msg2)) == 0);
}

void test_axi_driver_timeouts() {
    struct StuckBusyBus {
        uint32_t status_val{};
        static void wr(void*, uint32_t, uint32_t) {}
        static uint32_t rd(void* ctx, uint32_t) {
            return static_cast<StuckBusyBus*>(ctx)->status_val;
        }
    };

    // Bug #8 regression: sticky timeout flag on AxiSpiMaster, AxiOnfiNand, AxiCfiNor.
    StuckBusyBus sb{0x53500001u}; // "SP" magic + busy=1
    AxiBus bus{&sb, &StuckBusyBus::wr, &StuckBusyBus::rd};
    AxiSpiMaster spi;
    spi.set_max_busy_polls(4u);
    assert(!spi.initialize(bus, kDefaultBase + kSpiWindowOff, 0u));
    assert(spi.timed_out());
    spi.clear_timeout();
    assert(!spi.timed_out());

    sb.status_val = 0x4E410001u; // "NA" magic + busy=1
    AxiOnfiNand onfi;
    onfi.set_max_busy_polls(4u);
    assert(!onfi.initialize(bus, kDefaultBase + kOnfiNandOff));
    assert(onfi.timed_out());
    onfi.clear_timeout();
    assert(!onfi.timed_out());

    sb.status_val = 0x43460001u; // "CF" magic + busy=1
    AxiCfiNor cfi;
    cfi.set_max_busy_polls(4u);
    assert(!cfi.initialize(bus, kDefaultBase + kCfiNorOff));
    assert(cfi.timed_out());
    cfi.clear_timeout();
    assert(!cfi.timed_out());
}

void test_rtl_protocol_and_irq_regressions(FlashSocHarness& soc) {
    uint8_t resp = 0;

    // 1. Bug #2 regression: WVALID-before-AWVALID AND AWVALID-before-WVALID across all 3 windows!
    assert(soc.axi_write_w_before_aw(kDefaultBase + kSpiWindowOff + 0x00u, 0x00020001u) == 0u);
    assert(soc.axi_read(kDefaultBase + kSpiWindowOff + 0x00u, &resp) == 0x00020001u && resp == 0u);
    assert(soc.axi_write_aw_before_w(kDefaultBase + kOnfiNandOff + 0x1Cu, 0x00020302u) == 0u);
    assert(soc.axi_read(kDefaultBase + kOnfiNandOff + 0x1Cu, &resp) == 0x00020302u && resp == 0u);
    assert(soc.axi_write_w_before_aw(kDefaultBase + kCfiNorOff + 0x18u, 0x00020402u) == 0u);
    assert(soc.axi_read(kDefaultBase + kCfiNorOff + 0x18u, &resp) == 0x00020402u && resp == 0u);

    // 2. Bug #1 regression: SLVERR on aliased invalid offsets MUST NOT mutate registers or clear rx_valid!
    assert(soc.axi_write(kDefaultBase + kSpiWindowOff + 0x100u, 0x000500FFu) == 2u); // aliases 0x000
    assert(soc.axi_read(kDefaultBase + kSpiWindowOff + 0x00u, &resp) == 0x00020001u && resp == 0u);
    assert(soc.axi_write(kDefaultBase + kOnfiNandOff + 0x11Cu, 0x00090909u) == 2u); // aliases 0x01C
    assert(soc.axi_read(kDefaultBase + kOnfiNandOff + 0x1Cu, &resp) == 0x00020302u && resp == 0u);
    assert(soc.axi_write(kDefaultBase + kCfiNorOff + 0x118u, 0x00080808u) == 2u); // aliases 0x018
    assert(soc.axi_read(kDefaultBase + kCfiNorOff + 0x18u, &resp) == 0x00020402u && resp == 0u);

    // Trigger 1 SPI byte so rx_valid=1, then read invalid aliased offset 0x10C: must SLVERR & keep rx_valid=1!
    assert(soc.axi_write(kDefaultBase + kSpiWindowOff + 0x08u, 0xA5u) == 0u);
    while ((soc.axi_read(kDefaultBase + kSpiWindowOff + 0x04u, &resp) & 1u) != 0u) {}
    assert((soc.axi_read(kDefaultBase + kSpiWindowOff + 0x04u, &resp) & 2u) != 0u); // rx_valid == 1
    (void)soc.axi_read(kDefaultBase + kSpiWindowOff + 0x10Cu, &resp);
    assert(resp == 2u);
    assert((soc.axi_read(kDefaultBase + kSpiWindowOff + 0x04u, &resp) & 2u) != 0u); // STILL 1!
    (void)soc.axi_read(kDefaultBase + kSpiWindowOff + 0x0Cu, &resp);
    assert(resp == 0u);
    assert((soc.axi_read(kDefaultBase + kSpiWindowOff + 0x04u, &resp) & 2u) == 0u); // Now cleared!

    // 3. IRQ assertion & W1C clearing across SPI, ONFI, and CFI controllers!
    assert(soc.axi_write(kDefaultBase + kSpiWindowOff + 0x14u, 1u) == 0u); // Clear stale SPI IRQ
    assert(soc.axi_write(kDefaultBase + kSpiWindowOff + 0x10u, 1u) == 0u); // Enable SPI IRQ
    assert(soc.dut.flash_irq == 0u);
    assert(soc.axi_write(kDefaultBase + kSpiWindowOff + 0x08u, 0xFFu) == 0u);
    while ((soc.axi_read(kDefaultBase + kSpiWindowOff + 0x04u, &resp) & 1u) != 0u) {}
    assert(soc.dut.flash_irq == 1u);
    assert((soc.axi_read(kDefaultBase + kSubsysInfoOff + 0x08u, &resp) & 1u) != 0u);
    assert(soc.axi_write(kDefaultBase + kSpiWindowOff + 0x14u, 1u) == 0u); // W1C
    assert(soc.axi_write(kDefaultBase + kSpiWindowOff + 0x10u, 0u) == 0u);
    assert(soc.dut.flash_irq == 0u);

    // ONFI IRQ assertion & W1C clear
    assert(soc.axi_write(kDefaultBase + kOnfiNandOff + 0x24u, 3u) == 0u);
    assert(soc.axi_write(kDefaultBase + kOnfiNandOff + 0x00u, 0x0Bu) == 0u); // CE=1, WP#=1, IRQ_EN=1
    assert(soc.axi_write(kDefaultBase + kOnfiNandOff + 0x08u, 0xFFu) == 0u); // Reset cmd
    while ((soc.axi_read(kDefaultBase + kOnfiNandOff + 0x04u, &resp) & 1u) != 0u) {}
    assert(soc.dut.flash_irq == 1u);
    assert((soc.axi_read(kDefaultBase + kSubsysInfoOff + 0x08u, &resp) & 2u) != 0u);
    assert(soc.axi_write(kDefaultBase + kOnfiNandOff + 0x24u, 3u) == 0u); // W1C
    assert(soc.axi_write(kDefaultBase + kOnfiNandOff + 0x00u, 0x03u) == 0u);
    assert(soc.dut.flash_irq == 0u);

    // CFI IRQ assertion & W1C clear
    assert(soc.axi_write(kDefaultBase + kCfiNorOff + 0x1Cu, 1u) == 0u);
    assert(soc.axi_write(kDefaultBase + kCfiNorOff + 0x00u, 0x07u) == 0u); // RESET#=1, WP#=1, IRQ_EN=1
    assert(soc.axi_write(kDefaultBase + kCfiNorOff + 0x08u, 0u) == 0u);
    assert(soc.axi_write(kDefaultBase + kCfiNorOff + 0x10u, 1u) == 0u); // Trigger read16
    while ((soc.axi_read(kDefaultBase + kCfiNorOff + 0x04u, &resp) & 1u) != 0u) {}
    assert(soc.dut.flash_irq == 1u);
    assert((soc.axi_read(kDefaultBase + kSubsysInfoOff + 0x08u, &resp) & 4u) != 0u);
    assert(soc.axi_write(kDefaultBase + kCfiNorOff + 0x1Cu, 1u) == 0u); // W1C
    assert(soc.axi_write(kDefaultBase + kCfiNorOff + 0x00u, 0x03u) == 0u);
    assert(soc.dut.flash_irq == 0u);
}

} // namespace

int main() {
    test_axi_driver_timeouts();

    FlashSocHarness soc;
    soc.init();

    // 1. Verify AXI4-Lite subsystem magic, capabilities, and SLVERR on invalid addresses
    uint8_t resp = 0;
    const uint32_t magic = soc.axi_read(kDefaultBase + kSubsysInfoOff + 0x00u, &resp);
    assert(resp == 0u && magic == kSubsysMagic);
    const uint32_t caps = soc.axi_read(kDefaultBase + kSubsysInfoOff + 0x04u, &resp);
    assert(resp == 0u && (caps & 0xFu) == 0xFu); // SPI + ONFI + CFI + HW_ECC enabled
    // Unaligned & out-of-window accesses must return AXI SLVERR (2'b10)
    (void)soc.axi_read(kDefaultBase + kSubsysInfoOff + 0x02u, &resp);
    assert(resp == 2u);
    (void)soc.axi_read(0x20000000u, &resp);
    assert(resp == 2u);
    assert(soc.axi_write(kDefaultBase + kSubsysInfoOff + 0x00u, 0x1234u) == 2u); // RO register

    test_rtl_protocol_and_irq_regressions(soc);

    const AxiBus bus = soc.axi_bus();

    // 2a. Bug #3 regression: SPI Mode 3 (CPOL=1, CPHA=1) & Mode 1 (CPOL=0, CPHA=1) transfers + hold-time check!
    for (uint8_t spi_mode : {uint8_t(3u), uint8_t(1u)}) {
        AxiSpiMaster spi_mode_drv;
        assert(spi_mode_drv.initialize(bus, kDefaultBase + kSpiWindowOff, 0u, 2u, spi_mode));
        soc.spi0_nor.set_mode(spi_mode);
        SerialNorAdapter mode_nor;
        assert(mode_nor.initialize(spi_mode_drv.spi_bus(), 8u, 4u, 256u));
        uint8_t id[3]{};
        assert(mode_nor.read_jedec_id(id));
        assert(id[0] == 0xEFu && id[1] == 0x40u && id[2] == 0x18u);

        // Also program & read back a 256-byte page in CPHA=1 mode!
        uint8_t wr[256]{}, rd[256]{};
        for (size_t i = 0; i < 256; ++i) wr[i] = static_cast<uint8_t>((i * 29u + spi_mode * 7u) ^ 0x5Au);
        FlashBackend b = mode_nor.backend();
        assert(b.erase_block(b.context, 0u) == IoStatus::ok);
        assert(b.program_page(b.context, 0u, 0u, wr, 256u, nullptr, 0u) == IoStatus::ok);
        assert(b.read_page(b.context, 0u, 0u, rd, 256u, nullptr, 0u) == IoStatus::ok);
        assert(std::memcmp(wr, rd, 256u) == 0);
    }
    soc.nor_chip.reset_erased();
    soc.spi0_nor.set_mode(0u);

    // 2b. Serial SPI NOR (CS0#) end-to-end through AXI4-Lite SPI Master RTL (Mode 0)
    AxiSpiMaster spi_nor_drv;
    assert(spi_nor_drv.initialize(bus, kDefaultBase + kSpiWindowOff, 0u, 1u, 0u));
    SerialNorAdapter nor_adapter;
    assert(nor_adapter.initialize(spi_nor_drv.spi_bus(), 8u, 4u, 256u));
    uint8_t jedec[3]{};
    assert(nor_adapter.read_jedec_id(jedec));
    assert(jedec[0] == 0xEFu && jedec[1] == 0x40u && jedec[2] == 0x18u); // Winbond W25Q128JV
    verify_fs_roundtrip("SPI-NOR", nor_adapter.backend());
    assert(!spi_nor_drv.timed_out());
    assert(soc.nor_chip.telemetry().program_ops > 0u && soc.nor_chip.telemetry().erase_ops > 0u);

    // 3. Serial SPI SLC NAND (CS1#) with factory bad block through AXI4-Lite SPI Master RTL
    soc.snand_chip.mark_factory_bad(2u);
    AxiSpiMaster spi_nand_drv;
    assert(spi_nand_drv.initialize(bus, kDefaultBase + kSpiWindowOff, 1u, 1u, 0u));
    SerialNandAdapter snand_adapter;
    assert(snand_adapter.initialize(spi_nand_drv.spi_bus(), 8u, 4u, 256u, 32u));
    verify_fs_roundtrip("SPI-NAND", snand_adapter.backend());
    assert(!spi_nand_drv.timed_out());
    assert(soc.snand_chip.telemetry().program_ops > 0u);

    // 4. Parallel ONFI x8 NAND through AXI4-Lite ONFI Controller RTL + HW Hamming-256 ECC offload!
    soc.pnand_chip.mark_factory_bad(3u);
    AxiOnfiNand onfi_drv;
    assert(onfi_drv.initialize(bus, kDefaultBase + kOnfiNandOff, 1u, 2u, 1u));
    assert(onfi_drv.has_hw_ecc());
    ParallelNandBus nb_hw = onfi_drv.nand_bus();
    assert(nb_hw.hw_ecc_reset != nullptr && nb_hw.hw_ecc_read != nullptr);

    // Prove Verilog HW 22-bit Hamming-256 ECC engine is bit-exact with software hamming256_encode()!
    uint8_t test_block[256]{};
    for (uint32_t seed = 1; seed <= 5; ++seed) {
        for (size_t i = 0; i < 256; ++i) {
            test_block[i] = static_cast<uint8_t>((i * 37u + seed * 113u) ^ (i >> 1));
        }
        onfi_drv.hw_ecc_reset();
        nb_hw.write_cmd(nb_hw.context, 0x80u);
        nb_hw.write_addr(nb_hw.context, 0u);
        nb_hw.write_addr(nb_hw.context, 0u);
        nb_hw.write_addr(nb_hw.context, 0u);
        nb_hw.write_addr(nb_hw.context, 0u);
        nb_hw.write_addr(nb_hw.context, 0u);
        nb_hw.write_data(nb_hw.context, test_block, 256u);

        uint8_t hw_ecc[3]{}, sw_ecc[3]{};
        onfi_drv.hw_ecc_read(hw_ecc);
        hamming256_encode(test_block, sw_ecc);
        assert(std::memcmp(hw_ecc, sw_ecc, 3u) == 0);
    }
    soc.pnand_chip.reset_erased();
    soc.pnand_chip.mark_factory_bad(3u);

    // Prove HW-ECC-offloaded ParallelNandAdapter corrects a 1-bit flip & flags 2-bit uncorrectable!
    ParallelNandAdapter pnand_adapter;
    assert(pnand_adapter.initialize(nb_hw, 8u, 4u, 256u, 32u, true));
    {
        FlashBackend b = pnand_adapter.backend();
        uint8_t wr[256]{}, rd[256]{};
        for (size_t i = 0; i < 256; ++i) wr[i] = static_cast<uint8_t>((i * 19u + 7u) ^ 0xA5u);
        assert(b.program_page(b.context, 0u, 0u, wr, 256u, nullptr, 0u) == IoStatus::ok);
        soc.pnand_chip.flip_data_bit(0u, 0u, 137u, 0x10u); // Inject 1-bit upset
        assert(b.read_page(b.context, 0u, 0u, rd, 256u, nullptr, 0u) == IoStatus::ecc_corrected);
        assert(std::memcmp(wr, rd, 256u) == 0);
        soc.pnand_chip.flip_data_bit(0u, 0u, 12u, 0x01u);  // Now 2 bits flipped -> uncorrectable
        assert(b.read_page(b.context, 0u, 0u, rd, 256u, nullptr, 0u) == IoStatus::ecc_uncorrectable);
    }
    soc.pnand_chip.reset_erased();
    soc.pnand_chip.mark_factory_bad(3u);

    verify_fs_roundtrip("ONFI-NAND", pnand_adapter.backend());
    assert(!onfi_drv.timed_out());
    assert(soc.pnand_chip.telemetry().program_ops > 0u);

    // 5. Parallel 16-bit CFI NOR through AXI4-Lite CFI Controller RTL
    AxiCfiNor cfi_drv;
    assert(cfi_drv.initialize(bus, kDefaultBase + kCfiNorOff, 1u, 2u, 1u));
    ParallelNorAdapter pnor_adapter;
    assert(pnor_adapter.initialize(cfi_drv.nor_bus(), 8u, 4u, 256u));
    verify_fs_roundtrip("CFI-NOR", pnor_adapter.backend());
    assert(!cfi_drv.timed_out());
    assert(soc.pnor_chip.telemetry().program_ops > 0u && soc.pnor_chip.telemetry().erase_ops > 0u);

    std::printf("PASS: Verilator AXI4-Lite Multi-Flash RTL Co-Sim verified all 4 flash families, AXI W/AW ordering, SLVERR non-mutation, SPI Mode 1/3, IRQs, and HW Hamming-256 ECC offload (%llu RTL cycles).\n",
                static_cast<unsigned long long>(soc.cycles));
    return 0;
}
