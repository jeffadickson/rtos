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
/// to our SerialNorMock / SerialNandMock SpiBus protocol models.
class SpiPinTransceiver {
    SpiBus target_{};
    bool prev_cs_n_{true};
    bool prev_sclk_{false};
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
    void attach(const SpiBus& bus, bool is_nand) {
        target_ = bus;
        is_nand_ = is_nand;
        prev_cs_n_ = true;
        prev_sclk_ = false;
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
        }

        // Mode 0: sample MOSI on SCLK rising edge; shift MISO on SCLK falling edge
        if (sclk && !prev_sclk_) {
            rx_byte_ = static_cast<uint8_t>((rx_byte_ << 1) | (mosi ? 1u : 0u));
            ++rx_bits_;
            if (rx_bits_ == 8u) {
                if (!serving_response_) {
                    tx_frame_.push_back(rx_byte_);
                    const size_t hdr = read_header_len(tx_frame_, is_nand_);
                    if (hdr > 0 && tx_frame_.size() == hdr) {
                        // Prefetch response bytes from the chip emulator on read commands
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
        } else if (!sclk && prev_sclk_) {
            if (serving_response_) {
                if (!resp_armed_) {
                    // Trailing falling edge of last command/header bit: present bit 7 of resp[0]
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

} // namespace

int main() {
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

    const AxiBus bus = soc.axi_bus();

    // 2. Serial SPI NOR (CS0#) end-to-end through AXI4-Lite SPI Master RTL
    AxiSpiMaster spi_nor_drv;
    assert(spi_nor_drv.initialize(bus, kDefaultBase + kSpiWindowOff, 0u, 1u, 0u));
    SerialNorAdapter nor_adapter;
    assert(nor_adapter.initialize(spi_nor_drv.spi_bus(), 8u, 4u, 256u));
    uint8_t jedec[3]{};
    assert(nor_adapter.read_jedec_id(jedec));
    assert(jedec[0] == 0xEFu && jedec[1] == 0x40u && jedec[2] == 0x18u); // Winbond W25Q128JV
    verify_fs_roundtrip("SPI-NOR", nor_adapter.backend());
    assert(soc.nor_chip.telemetry().program_ops > 0u && soc.nor_chip.telemetry().erase_ops > 0u);

    // 3. Serial SPI SLC NAND (CS1#) with factory bad block through AXI4-Lite SPI Master RTL
    soc.snand_chip.mark_factory_bad(2u);
    AxiSpiMaster spi_nand_drv;
    assert(spi_nand_drv.initialize(bus, kDefaultBase + kSpiWindowOff, 1u, 1u, 0u));
    SerialNandAdapter snand_adapter;
    assert(snand_adapter.initialize(spi_nand_drv.spi_bus(), 8u, 4u, 256u, 32u));
    verify_fs_roundtrip("SPI-NAND", snand_adapter.backend());
    assert(soc.snand_chip.telemetry().program_ops > 0u);

    // 4. Parallel ONFI x8 NAND through AXI4-Lite ONFI Controller RTL + HW Hamming-256 ECC verification!
    soc.pnand_chip.mark_factory_bad(3u);
    AxiOnfiNand onfi_drv;
    assert(onfi_drv.initialize(bus, kDefaultBase + kOnfiNandOff, 1u, 2u, 1u));
    assert(onfi_drv.has_hw_ecc());

    // Prove Verilog HW 22-bit Hamming-256 ECC engine is bit-exact with software hamming256_encode()!
    uint8_t test_block[256]{};
    for (uint32_t seed = 1; seed <= 5; ++seed) {
        for (size_t i = 0; i < 256; ++i) {
            test_block[i] = static_cast<uint8_t>((i * 37u + seed * 113u) ^ (i >> 1));
        }
        onfi_drv.hw_ecc_reset();
        ParallelNandBus nb = onfi_drv.nand_bus();
        nb.write_cmd(nb.context, 0x80u);
        nb.write_addr(nb.context, 0u);
        nb.write_addr(nb.context, 0u);
        nb.write_addr(nb.context, 0u);
        nb.write_addr(nb.context, 0u);
        nb.write_addr(nb.context, 0u);
        nb.write_data(nb.context, test_block, 256u);

        uint8_t hw_ecc[3]{}, sw_ecc[3]{};
        onfi_drv.hw_ecc_read(hw_ecc);
        hamming256_encode(test_block, sw_ecc);
        assert(std::memcmp(hw_ecc, sw_ecc, 3u) == 0);
    }
    soc.pnand_chip.reset_erased();
    soc.pnand_chip.mark_factory_bad(3u);

    ParallelNandAdapter pnand_adapter;
    assert(pnand_adapter.initialize(onfi_drv.nand_bus(), 8u, 4u, 256u, 32u, true));
    verify_fs_roundtrip("ONFI-NAND", pnand_adapter.backend());
    assert(soc.pnand_chip.telemetry().program_ops > 0u);

    // 5. Parallel 16-bit CFI NOR through AXI4-Lite CFI Controller RTL
    AxiCfiNor cfi_drv;
    assert(cfi_drv.initialize(bus, kDefaultBase + kCfiNorOff, 1u, 2u, 1u));
    ParallelNorAdapter pnor_adapter;
    assert(pnor_adapter.initialize(cfi_drv.nor_bus(), 8u, 4u, 256u));
    verify_fs_roundtrip("CFI-NOR", pnor_adapter.backend());
    assert(soc.pnor_chip.telemetry().program_ops > 0u && soc.pnor_chip.telemetry().erase_ops > 0u);

    std::printf("PASS: Verilator AXI4-Lite Multi-Flash RTL Co-Sim verified all 4 flash families + HW Hamming-256 ECC (%llu RTL cycles).\n",
                static_cast<unsigned long long>(soc.cycles));
    return 0;
}
