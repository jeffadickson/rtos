#pragma once
/** @file rtos_storage_axi.hpp
 * @brief Freestanding C++20 AXI4-Lite MMIO transport drivers for NexRT's
 *        synthesizable Verilog flash controller subsystem:
 *          - rtl/nexrt_spi_flash_axi.sv   (Serial SPI NOR on CS0#, Serial SPI NAND on CS1#)
 *          - rtl/nexrt_onfi_nand_axi.sv   (Parallel ONFI x8 NAND + HW 22-bit Hamming ECC)
 *          - rtl/nexrt_cfi_nor_axi.sv     (Parallel 16-bit CFI/JEDEC NOR)
 *          - rtl/nexrt_flash_subsys_axi.sv (Unified 16 KiB AXI4-Lite multi-flash wrapper)
 *
 * Zero heap, zero exceptions, zero RTTI. Uses an injectable 32-bit AXI register
 * bus hook so the exact same driver code runs on bare-metal MMIO (RISCue/ARM/
 * MicroBlaze) and directly inside Verilator AXI4-Lite co-simulation.
 */
#include <stddef.h>
#include <stdint.h>
#include "rtos_storage.hpp"

namespace rtos::storage::axi {

/// Default 16 KiB subsystem sub-window offsets matching rtl/nexrt_flash_subsys_axi.sv.
inline constexpr uint32_t kDefaultBase      = 0x10020000u;
inline constexpr uint32_t kSpiWindowOff     = 0x0000u;
inline constexpr uint32_t kOnfiNandOff      = 0x1000u;
inline constexpr uint32_t kCfiNorOff        = 0x2000u;
inline constexpr uint32_t kSubsysInfoOff    = 0x3000u;
inline constexpr uint32_t kSubsysMagic      = 0x4E584643u; // "NXFC"

/// 32-bit AXI4-Lite register accessor (bare-metal MMIO or Verilator AXI master).
struct AxiBus {
    void* context{};
    void (*write32)(void* ctx, uint32_t addr, uint32_t value){};
    uint32_t (*read32)(void* ctx, uint32_t addr){};

    void wr(uint32_t addr, uint32_t val) const { write32(context, addr, val); }
    uint32_t rd(uint32_t addr) const { return read32(context, addr); }
};

/// Direct bare-metal MMIO AXI4-Lite bus accessor (no context needed).
inline void mmio_write32(void*, uint32_t addr, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(addr)) = value;
}

inline uint32_t mmio_read32(void*, uint32_t addr) {
    return *reinterpret_cast<const volatile uint32_t*>(static_cast<uintptr_t>(addr));
}

inline constexpr AxiBus make_mmio_bus() {
    return AxiBus{nullptr, &mmio_write32, &mmio_read32};
}

// =============================================================================
// 1. AXI4-Lite SPI Master Transport (Serial SPI NOR & Serial SPI SLC NAND)
// =============================================================================
class AxiSpiMaster {
    AxiBus bus_{};
    uint32_t base_{kDefaultBase + kSpiWindowOff};
    uint8_t cs_index_{0};
    uint16_t clk_div_{1};
    uint8_t mode_{0}; // bit 0 = CPOL, bit 1 = CPHA
    uint32_t max_busy_polls_{100000u};
    mutable bool timed_out_{false};

    static constexpr uint32_t kRegCtrl    = 0x00u;
    static constexpr uint32_t kRegStatus  = 0x04u;
    static constexpr uint32_t kRegTxData  = 0x08u;
    static constexpr uint32_t kRegRxData  = 0x0Cu;
    static constexpr uint32_t kRegIrqEn   = 0x10u;
    static constexpr uint32_t kRegIrqStat = 0x14u;

    uint32_t ctrl_word(bool cs_assert) const {
        return 1u |
               (static_cast<uint32_t>(mode_ & 1u) << 1) |
               (static_cast<uint32_t>((mode_ >> 1) & 1u) << 2) |
               (static_cast<uint32_t>(cs_index_ & 0xFu) << 4) |
               (cs_assert ? (1u << 8) : 0u) |
               (static_cast<uint32_t>(clk_div_ == 0u ? 1u : clk_div_) << 16);
    }

    bool wait_idle() const {
        for (uint32_t i = 0; i < max_busy_polls_; ++i) {
            if ((bus_.rd(base_ + kRegStatus) & 1u) == 0u) return true;
        }
        timed_out_ = true;
        return false;
    }

    bool xfer_byte(uint8_t tx, uint8_t& rx) const {
        bus_.wr(base_ + kRegTxData, tx);
        if (!wait_idle()) return false;
        rx = static_cast<uint8_t>(bus_.rd(base_ + kRegRxData) & 0xFFu);
        return true;
    }

    static bool spi_transfer_cb(void* ctx, const uint8_t* tx, size_t tx_len,
                                uint8_t* rx, size_t rx_len) {
        const auto* self = static_cast<const AxiSpiMaster*>(ctx);
        if (!self || !self->bus_.write32 || !self->bus_.read32) return false;
        if ((!tx && tx_len) || (!rx && rx_len)) return false;
        if (!self->wait_idle()) return false;

        // Assert selected CS#
        self->bus_.wr(self->base_ + kRegCtrl, self->ctrl_word(true));
        bool ok = true;
        for (size_t i = 0; i < tx_len; ++i) {
            uint8_t dummy = 0;
            if (!self->xfer_byte(tx[i], dummy)) {
                ok = false;
                break;
            }
        }
        if (ok) {
            for (size_t i = 0; i < rx_len; ++i) {
                if (!self->xfer_byte(0xFFu, rx[i])) {
                    ok = false;
                    break;
                }
            }
        }
        // Always deassert CS# before returning
        self->bus_.wr(self->base_ + kRegCtrl, self->ctrl_word(false));
        return ok;
    }

public:
    constexpr AxiSpiMaster() = default;

    bool initialize(const AxiBus& bus, uint32_t base_addr, uint8_t cs_index,
                    uint16_t clk_div = 1u, uint8_t spi_mode = 0u) {
        if (!bus.write32 || !bus.read32 || cs_index >= 16u) return false;
        bus_ = bus;
        base_ = base_addr;
        cs_index_ = cs_index;
        clk_div_ = clk_div == 0u ? 1u : clk_div;
        mode_ = spi_mode;
        timed_out_ = false;
        const uint32_t st = bus_.rd(base_ + kRegStatus);
        if ((st >> 16) != 0x5350u) return false; // "SP" magic check
        bus_.wr(base_ + kRegCtrl, ctrl_word(false));
        bus_.wr(base_ + kRegIrqEn, 0u);
        bus_.wr(base_ + kRegIrqStat, 1u);
        return wait_idle();
    }

    void set_max_busy_polls(uint32_t polls) { max_busy_polls_ = polls; }
    bool timed_out() const { return timed_out_; }
    void clear_timeout() { timed_out_ = false; }

    SpiBus spi_bus(uint32_t max_poll = 100000u) {
        return SpiBus{this, &spi_transfer_cb, max_poll};
    }
};

// =============================================================================
// 2. AXI4-Lite Parallel ONFI 1.0 x8 NAND Transport + HW Hamming-256 ECC
// =============================================================================
class AxiOnfiNand {
    AxiBus bus_{};
    uint32_t base_{kDefaultBase + kOnfiNandOff};
    uint32_t max_busy_polls_{100000u};
    mutable bool timed_out_{false};

    static constexpr uint32_t kRegCtrl     = 0x00u;
    static constexpr uint32_t kRegStatus   = 0x04u;
    static constexpr uint32_t kRegCmd      = 0x08u;
    static constexpr uint32_t kRegAddr     = 0x0Cu;
    static constexpr uint32_t kRegDataWr   = 0x10u;
    static constexpr uint32_t kRegDataRdTr = 0x14u;
    static constexpr uint32_t kRegDataRd   = 0x18u;
    static constexpr uint32_t kRegTiming   = 0x1Cu;
    static constexpr uint32_t kRegEccCode  = 0x20u;
    static constexpr uint32_t kRegIrqStat  = 0x24u;

    bool wait_fsm() const {
        for (uint32_t i = 0; i < max_busy_polls_; ++i) {
            if ((bus_.rd(base_ + kRegStatus) & 1u) == 0u) return true;
        }
        timed_out_ = true;
        return false;
    }

    static void cmd_cb(void* ctx, uint8_t cmd) {
        const auto* self = static_cast<const AxiOnfiNand*>(ctx);
        self->bus_.wr(self->base_ + kRegCmd, cmd);
        (void)self->wait_fsm();
    }

    static void addr_cb(void* ctx, uint8_t addr) {
        const auto* self = static_cast<const AxiOnfiNand*>(ctx);
        self->bus_.wr(self->base_ + kRegAddr, addr);
        (void)self->wait_fsm();
    }

    static void wr_data_cb(void* ctx, const uint8_t* bytes, size_t len) {
        const auto* self = static_cast<const AxiOnfiNand*>(ctx);
        for (size_t i = 0; i < len; ++i) {
            self->bus_.wr(self->base_ + kRegDataWr, bytes[i]);
            if (!self->wait_fsm()) return;
        }
    }

    static void rd_data_cb(void* ctx, uint8_t* bytes, size_t len) {
        const auto* self = static_cast<const AxiOnfiNand*>(ctx);
        for (size_t i = 0; i < len; ++i) {
            self->bus_.wr(self->base_ + kRegDataRdTr, 1u);
            if (!self->wait_fsm()) return;
            bytes[i] = static_cast<uint8_t>(self->bus_.rd(self->base_ + kRegDataRd) & 0xFFu);
        }
    }

    static bool ready_cb(void* ctx) {
        const auto* self = static_cast<const AxiOnfiNand*>(ctx);
        return (self->bus_.rd(self->base_ + kRegStatus) & 2u) != 0u;
    }

    static void hw_ecc_reset_cb(void* ctx) {
        static_cast<const AxiOnfiNand*>(ctx)->hw_ecc_reset();
    }

    static void hw_ecc_read_cb(void* ctx, uint8_t ecc[3]) {
        static_cast<const AxiOnfiNand*>(ctx)->hw_ecc_read(ecc);
    }

public:
    constexpr AxiOnfiNand() = default;

    bool initialize(const AxiBus& bus, uint32_t base_addr,
                    uint8_t t_setup = 1u, uint8_t t_pulse = 2u, uint8_t t_hold = 1u) {
        if (!bus.write32 || !bus.read32) return false;
        bus_ = bus;
        base_ = base_addr;
        timed_out_ = false;
        const uint32_t st = bus_.rd(base_ + kRegStatus);
        if ((st >> 16) != 0x4E41u) return false; // "NA" magic check
        bus_.wr(base_ + kRegTiming,
                static_cast<uint32_t>(t_setup) |
                (static_cast<uint32_t>(t_pulse) << 8) |
                (static_cast<uint32_t>(t_hold) << 16));
        bus_.wr(base_ + kRegCtrl, 0x07u); // CE asserted, WP#=1, reset HW ECC
        bus_.wr(base_ + kRegIrqStat, 0x03u);
        return wait_fsm();
    }

    /// Reset hardware 22-bit Hamming-256 ECC engine before streaming 256 data bytes.
    void hw_ecc_reset() const {
        bus_.wr(base_ + kRegCtrl, 0x07u);
    }

    /// Read live hardware-computed 22-bit Hamming SECDED ECC (bit-exact with hamming256_encode).
    void hw_ecc_read(uint8_t ecc[3]) const {
        const uint32_t code = bus_.rd(base_ + kRegEccCode);
        ecc[0] = static_cast<uint8_t>(code & 0xFFu);
        ecc[1] = static_cast<uint8_t>((code >> 8) & 0xFFu);
        ecc[2] = static_cast<uint8_t>((code >> 16) & 0x3Fu);
    }

    bool has_hw_ecc() const {
        return (bus_.rd(base_ + kRegStatus) & 4u) != 0u;
    }

    void set_max_busy_polls(uint32_t polls) { max_busy_polls_ = polls; }
    bool timed_out() const { return timed_out_; }
    void clear_timeout() { timed_out_ = false; }

    ParallelNandBus nand_bus(uint32_t max_poll = 100000u) {
        const bool hw = has_hw_ecc();
        return ParallelNandBus{
            this, &cmd_cb, &addr_cb, &wr_data_cb, &rd_data_cb, &ready_cb, max_poll,
            hw ? &hw_ecc_reset_cb : nullptr,
            hw ? &hw_ecc_read_cb : nullptr
        };
    }
};

// =============================================================================
// 3. AXI4-Lite Parallel 16-bit CFI/JEDEC NOR Transport
// =============================================================================
class AxiCfiNor {
    AxiBus bus_{};
    uint32_t base_{kDefaultBase + kCfiNorOff};
    uint32_t max_busy_polls_{100000u};
    mutable bool timed_out_{false};

    static constexpr uint32_t kRegCtrl    = 0x00u;
    static constexpr uint32_t kRegStatus  = 0x04u;
    static constexpr uint32_t kRegAddr    = 0x08u;
    static constexpr uint32_t kRegWdata   = 0x0Cu;
    static constexpr uint32_t kRegRdTrig  = 0x10u;
    static constexpr uint32_t kRegRdata   = 0x14u;
    static constexpr uint32_t kRegTiming  = 0x18u;
    static constexpr uint32_t kRegIrqStat = 0x1Cu;

    bool wait_fsm() const {
        for (uint32_t i = 0; i < max_busy_polls_; ++i) {
            if ((bus_.rd(base_ + kRegStatus) & 1u) == 0u) return true;
        }
        timed_out_ = true;
        return false;
    }

    static void write16_cb(void* ctx, uint32_t word_offset, uint16_t value) {
        const auto* self = static_cast<const AxiCfiNor*>(ctx);
        self->bus_.wr(self->base_ + kRegAddr, word_offset);
        self->bus_.wr(self->base_ + kRegWdata, value);
        (void)self->wait_fsm();
    }

    static uint16_t read16_cb(void* ctx, uint32_t word_offset) {
        const auto* self = static_cast<const AxiCfiNor*>(ctx);
        self->bus_.wr(self->base_ + kRegAddr, word_offset);
        self->bus_.wr(self->base_ + kRegRdTrig, 1u);
        if (!self->wait_fsm()) return 0xFFFFu;
        return static_cast<uint16_t>(self->bus_.rd(self->base_ + kRegRdata) & 0xFFFFu);
    }

public:
    constexpr AxiCfiNor() = default;

    bool initialize(const AxiBus& bus, uint32_t base_addr,
                    uint8_t t_setup = 1u, uint8_t t_pulse = 2u, uint8_t t_hold = 1u) {
        if (!bus.write32 || !bus.read32) return false;
        bus_ = bus;
        base_ = base_addr;
        timed_out_ = false;
        const uint32_t st = bus_.rd(base_ + kRegStatus);
        if ((st >> 16) != 0x4346u) return false; // "CF" magic check
        bus_.wr(base_ + kRegTiming,
                static_cast<uint32_t>(t_setup) |
                (static_cast<uint32_t>(t_pulse) << 8) |
                (static_cast<uint32_t>(t_hold) << 16));
        bus_.wr(base_ + kRegCtrl, 0x03u); // RESET#=1, WP#=1
        bus_.wr(base_ + kRegIrqStat, 0x01u);
        return wait_fsm();
    }

    void set_max_busy_polls(uint32_t polls) { max_busy_polls_ = polls; }
    bool timed_out() const { return timed_out_; }
    void clear_timeout() { timed_out_ = false; }

    ParallelNorBus nor_bus(uint32_t max_poll = 200000u) {
        return ParallelNorBus{this, &write16_cb, &read16_cb, max_poll};
    }
};

} // namespace rtos::storage::axi
