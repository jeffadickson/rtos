#pragma once
/** @file rtos_flash_mocks.hpp
 * @brief Boot-static, zero-heap, freestanding (-nostdlib) hardware emulators for
 *        all four flash memory families supported by RTOS_STORAGE:
 *          1. SerialNorMock   (Winbond W25Q128JV / MX25L12835F / S25FL128L)
 *          2. SerialNandMock  (Winbond W25N01GV / Micron MT29F1G01ABAFD)
 *          3. ParallelNandMock(ONFI 1.0 x8: Micron MT29F4G08ABA / TC58NVG2S0H)
 *          4. ParallelNorMock (16-bit CFI/JEDEC: Infineon S29GL128S / MT28EW128ABA)
 *
 * Usable identically in host unit tests, QEMU Cortex-M3 / RISCue simulations,
 * and hardware-in-the-loop bringup without heap allocation or STL headers.
 * Models true flash 1->0 AND-programming physics, WIP/OIP/DQ7 busy polling,
 * factory/runtime bad blocks, bit-flip injection, and mid-operation power cuts.
 */
#include <stddef.h>
#include <stdint.h>
#include "rtos_storage.hpp"

namespace rtos::storage::mocks {

/// Programmable fault & timing injection shared by all four flash emulators.
struct FaultConfig {
    uint32_t busy_polls{};          // Simulated WIP / OIP / R/B# / DQ7 busy cycles per op
    int32_t fail_program_block{-1}; // Inject P_FAIL / status fail on this block (-1 = none)
    int32_t fail_erase_block{-1};   // Inject E_FAIL / status fail on this block (-1 = none)
    int32_t power_cut_after_ops{-1};// Cut power after N program/erase ops (-1 = disabled)
    size_t torn_program_bytes{24u}; // Partial bytes written on mid-program power cut (<40 B RecordHeader)
    bool power_lost{};              // Latched true once a power cut triggers
};

/// Live observability counters for emulation tests and wear verification.
struct ChipTelemetry {
    uint32_t read_ops{};
    uint32_t program_ops{};
    uint32_t erase_ops{};
    uint32_t busy_polls_served{};
    uint32_t torn_cuts{};
    uint8_t last_opcode{};
};

// =============================================================================
// 1. Serial SPI/QSPI NOR Emulator (Winbond W25Q128JV / MX25L12835F / S25FL128L)
// =============================================================================
template<size_t Blocks, size_t PagesPerBlock, size_t PageBytes>
class SerialNorMock {
    static_assert(Blocks >= 3 && PagesPerBlock >= 1 && PageBytes >= 128);
    static constexpr size_t kBlockBytes = PagesPerBlock * PageBytes;
    static constexpr size_t kTotalBytes = Blocks * kBlockBytes;

    uint8_t mem_[kTotalBytes]{};
    uint32_t block_erases_[Blocks]{};
    uint8_t jedec_id_[3]{0xEFu, 0x40u, 0x18u}; // Default: Winbond W25Q128JV
    bool wel_{};
    uint32_t busy_remaining_{};
    FaultConfig fault_{};
    ChipTelemetry stats_{};

    static bool spi_transfer(void* ctx, const uint8_t* tx, size_t tx_len,
                             uint8_t* rx, size_t rx_len) {
        auto* s = static_cast<SerialNorMock*>(ctx);
        if (!s || !tx || tx_len == 0u || s->fault_.power_lost) return false;
        const uint8_t op = tx[0];
        s->stats_.last_opcode = op;

        if (op == 0x9Fu && rx && rx_len >= 3u) { // JEDEC ID
            rx[0] = s->jedec_id_[0];
            rx[1] = s->jedec_id_[1];
            rx[2] = s->jedec_id_[2];
            return true;
        }
        if (op == 0x06u) { s->wel_ = true; return true; }  // WREN
        if (op == 0x04u) { s->wel_ = false; return true; } // WRDI
        if (op == 0x05u && rx && rx_len >= 1u) {           // RDSR1
            uint8_t sr = s->wel_ ? 0x02u : 0x00u;
            if (s->busy_remaining_ > 0u) {
                --s->busy_remaining_;
                ++s->stats_.busy_polls_served;
                sr |= 0x01u; // WIP bit
            }
            rx[0] = sr;
            return true;
        }
        if (s->busy_remaining_ > 0u) return false; // Busy chip ignores array commands

        if (op == 0x03u && tx_len == 4u && rx) { // Read Data
            const uint32_t addr = (uint32_t(tx[1]) << 16) | (uint32_t(tx[2]) << 8) | tx[3];
            if (size_t(addr) + rx_len > kTotalBytes) return false;
            for (size_t i = 0; i < rx_len; ++i) rx[i] = s->mem_[addr + i];
            ++s->stats_.read_ops;
            return true;
        }
        if (op == 0x02u && tx_len > 4u) { // Page Program (<=256 B, AND physics)
            if (!s->wel_) return false;
            s->wel_ = false;
            const uint32_t addr = (uint32_t(tx[1]) << 16) | (uint32_t(tx[2]) << 8) | tx[3];
            const size_t n = tx_len - 4u;
            if (n > 256u || size_t(addr) + n > kTotalBytes) return false;
            const uint32_t blk = addr / static_cast<uint32_t>(kBlockBytes);
            if (static_cast<int32_t>(blk) == s->fault_.fail_program_block) return false;

            if (s->fault_.power_cut_after_ops == 0) {
                const size_t torn = n < s->fault_.torn_program_bytes ? n : s->fault_.torn_program_bytes;
                for (size_t i = 0; i < torn; ++i) s->mem_[addr + i] &= tx[4u + i];
                s->fault_.power_lost = true;
                ++s->stats_.torn_cuts;
                return false;
            }
            if (s->fault_.power_cut_after_ops > 0) --s->fault_.power_cut_after_ops;

            for (size_t i = 0; i < n; ++i) s->mem_[addr + i] &= tx[4u + i];
            s->busy_remaining_ = s->fault_.busy_polls;
            ++s->stats_.program_ops;
            return true;
        }
        if ((op == 0x20u || op == 0xD8u) && tx_len == 4u) { // Sector (4 KiB) / Block (64 KiB) Erase
            if (!s->wel_) return false;
            s->wel_ = false;
            const uint32_t addr = (uint32_t(tx[1]) << 16) | (uint32_t(tx[2]) << 8) | tx[3];
            const size_t erase_len = (op == 0x20u && kBlockBytes <= 4096u) ? kBlockBytes
                                                                           : (op == 0x20u ? 4096u : kBlockBytes);
            if (size_t(addr) + erase_len > kTotalBytes) return false;
            const uint32_t blk = addr / static_cast<uint32_t>(kBlockBytes);
            if (static_cast<int32_t>(blk) == s->fault_.fail_erase_block) return false;

            if (s->fault_.power_cut_after_ops == 0) {
                s->mem_[addr] ^= 0x55u; // Corrupted half-erased block
                s->fault_.power_lost = true;
                ++s->stats_.torn_cuts;
                return false;
            }
            if (s->fault_.power_cut_after_ops > 0) --s->fault_.power_cut_after_ops;

            for (size_t i = 0; i < erase_len; ++i) s->mem_[addr + i] = 0xFFu;
            if (blk < Blocks) ++s->block_erases_[blk];
            s->busy_remaining_ = s->fault_.busy_polls;
            ++s->stats_.erase_ops;
            return true;
        }
        return false;
    }

public:
    constexpr SerialNorMock() = default;

    void reset_erased() {
        for (size_t i = 0; i < kTotalBytes; ++i) mem_[i] = 0xFFu;
        for (size_t b = 0; b < Blocks; ++b) block_erases_[b] = 0u;
        wel_ = false;
        busy_remaining_ = 0u;
        fault_ = {};
        stats_ = {};
    }

    void set_jedec_id(uint8_t mfr, uint8_t type, uint8_t cap) {
        jedec_id_[0] = mfr;
        jedec_id_[1] = type;
        jedec_id_[2] = cap;
    }

    void restore_power() {
        fault_.power_lost = false;
        fault_.power_cut_after_ops = -1;
        wel_ = false;
        busy_remaining_ = 0u;
    }

    void flip_bit(uint32_t block, uint32_t page, size_t byte_off, uint8_t mask) {
        const size_t idx = (size_t(block) * PagesPerBlock + page) * PageBytes + byte_off;
        if (idx < kTotalBytes) mem_[idx] ^= mask;
    }

    FaultConfig& faults() { return fault_; }
    const ChipTelemetry& telemetry() const { return stats_; }
    uint32_t block_erase_count(size_t b) const { return b < Blocks ? block_erases_[b] : 0u; }
    SpiBus spi_bus(uint32_t max_poll = 100000u) { return {this, &spi_transfer, max_poll}; }
};

// =============================================================================
// 2. Serial SPI SLC NAND Emulator (Winbond W25N01GV / Micron MT29F1G01ABAFD)
// =============================================================================
template<size_t Blocks, size_t PagesPerBlock, size_t PageBytes, size_t SpareBytes = 64u>
class SerialNandMock {
    static_assert(Blocks >= 3 && PagesPerBlock >= 1 && PageBytes >= 128 && SpareBytes >= 4);
    static constexpr size_t kTotalPages = Blocks * PagesPerBlock;

    uint8_t data_[kTotalPages * PageBytes]{};
    uint8_t spare_[kTotalPages * SpareBytes]{};
    uint8_t cache_[PageBytes + SpareBytes]{};
    uint32_t block_erases_[Blocks]{};
    uint8_t jedec_id_[3]{0xEFu, 0xAAu, 0x21u}; // Default: Winbond W25N01GV
    bool wel_{};
    uint8_t status_{};
    uint32_t busy_remaining_{};
    int32_t inject_ecc_row_{-1};
    uint8_t inject_ecc_bits_{}; // 1=corrected, 2=uncorrectable
    FaultConfig fault_{};
    ChipTelemetry stats_{};

    static bool spi_transfer(void* ctx, const uint8_t* tx, size_t tx_len,
                             uint8_t* rx, size_t rx_len) {
        auto* s = static_cast<SerialNandMock*>(ctx);
        if (!s || !tx || tx_len == 0u || s->fault_.power_lost) return false;
        const uint8_t op = tx[0];
        s->stats_.last_opcode = op;

        if (op == 0x9Fu && rx && rx_len >= 3u) { // JEDEC ID
            rx[0] = s->jedec_id_[0];
            rx[1] = s->jedec_id_[1];
            rx[2] = s->jedec_id_[2];
            return true;
        }
        if (op == 0x06u) { s->wel_ = true; return true; }
        if (op == 0x04u) { s->wel_ = false; return true; }
        if (op == 0x0Fu && tx_len == 2u && rx && rx_len >= 1u) { // Get Feature (0xC0 status)
            uint8_t sr = static_cast<uint8_t>(s->status_ | (s->wel_ ? 0x02u : 0x00u));
            if (s->busy_remaining_ > 0u) {
                --s->busy_remaining_;
                ++s->stats_.busy_polls_served;
                sr |= 0x01u; // OIP bit
            }
            rx[0] = sr;
            return true;
        }
        if (s->busy_remaining_ > 0u) return false;

        if (op == 0x13u && tx_len == 4u) { // Page Data Read -> internal cache
            const uint32_t row = (uint32_t(tx[1]) << 16) | (uint32_t(tx[2]) << 8) | tx[3];
            if (row >= kTotalPages) return false;
            for (size_t i = 0; i < PageBytes; ++i) s->cache_[i] = s->data_[size_t(row) * PageBytes + i];
            for (size_t i = 0; i < SpareBytes; ++i) s->cache_[PageBytes + i] = s->spare_[size_t(row) * SpareBytes + i];
            s->status_ = (static_cast<int32_t>(row) == s->inject_ecc_row_)
                             ? static_cast<uint8_t>((s->inject_ecc_bits_ & 0x03u) << 4)
                             : 0x00u;
            s->busy_remaining_ = s->fault_.busy_polls;
            ++s->stats_.read_ops;
            return true;
        }
        if (op == 0x03u && tx_len == 4u && rx) { // Read from cache (col + 1 dummy byte)
            const uint32_t col = (uint32_t(tx[1]) << 8) | tx[2];
            if (size_t(col) + rx_len > sizeof(s->cache_)) return false;
            for (size_t i = 0; i < rx_len; ++i) rx[i] = s->cache_[col + i];
            return true;
        }
        if ((op == 0x02u || op == 0x84u) && tx_len >= 3u) { // Program Load / Random Program Load
            if (op == 0x02u) {
                for (size_t i = 0; i < sizeof(s->cache_); ++i) s->cache_[i] = 0xFFu;
            }
            const uint32_t col = (uint32_t(tx[1]) << 8) | tx[2];
            const size_t n = tx_len - 3u;
            if (size_t(col) + n > sizeof(s->cache_)) return false;
            for (size_t i = 0; i < n; ++i) s->cache_[col + i] = tx[3u + i];
            return true;
        }
        if (op == 0x10u && tx_len == 4u) { // Program Execute
            if (!s->wel_) return false;
            s->wel_ = false;
            const uint32_t row = (uint32_t(tx[1]) << 16) | (uint32_t(tx[2]) << 8) | tx[3];
            if (row >= kTotalPages) return false;
            const uint32_t blk = row / static_cast<uint32_t>(PagesPerBlock);
            if (static_cast<int32_t>(blk) == s->fault_.fail_program_block) {
                s->status_ = 0x08u; // P_FAIL bit 3
                s->busy_remaining_ = s->fault_.busy_polls;
                return true;
            }
            if (s->fault_.power_cut_after_ops == 0) {
                const size_t torn = PageBytes < s->fault_.torn_program_bytes ? PageBytes : s->fault_.torn_program_bytes;
                for (size_t i = 0; i < torn; ++i) s->data_[size_t(row) * PageBytes + i] &= s->cache_[i];
                s->fault_.power_lost = true;
                ++s->stats_.torn_cuts;
                return false;
            }
            if (s->fault_.power_cut_after_ops > 0) --s->fault_.power_cut_after_ops;

            for (size_t i = 0; i < PageBytes; ++i) s->data_[size_t(row) * PageBytes + i] &= s->cache_[i];
            for (size_t i = 0; i < SpareBytes; ++i) s->spare_[size_t(row) * SpareBytes + i] &= s->cache_[PageBytes + i];
            s->status_ = 0x00u;
            s->busy_remaining_ = s->fault_.busy_polls;
            ++s->stats_.program_ops;
            return true;
        }
        if (op == 0xD8u && tx_len == 4u) { // Block Erase
            if (!s->wel_) return false;
            s->wel_ = false;
            const uint32_t row = (uint32_t(tx[1]) << 16) | (uint32_t(tx[2]) << 8) | tx[3];
            const uint32_t blk = row / static_cast<uint32_t>(PagesPerBlock);
            if (blk >= Blocks) return false;
            if (static_cast<int32_t>(blk) == s->fault_.fail_erase_block) {
                s->status_ = 0x04u; // E_FAIL bit 2
                s->busy_remaining_ = s->fault_.busy_polls;
                return true;
            }
            if (s->fault_.power_cut_after_ops == 0) {
                s->data_[size_t(blk) * PagesPerBlock * PageBytes] ^= 0x55u;
                s->fault_.power_lost = true;
                ++s->stats_.torn_cuts;
                return false;
            }
            if (s->fault_.power_cut_after_ops > 0) --s->fault_.power_cut_after_ops;

            const size_t d_base = size_t(blk) * PagesPerBlock * PageBytes;
            const size_t s_base = size_t(blk) * PagesPerBlock * SpareBytes;
            for (size_t i = 0; i < PagesPerBlock * PageBytes; ++i) s->data_[d_base + i] = 0xFFu;
            for (size_t i = 0; i < PagesPerBlock * SpareBytes; ++i) s->spare_[s_base + i] = 0xFFu;
            ++s->block_erases_[blk];
            s->status_ = 0x00u;
            s->busy_remaining_ = s->fault_.busy_polls;
            ++s->stats_.erase_ops;
            return true;
        }
        return false;
    }

public:
    constexpr SerialNandMock() = default;

    void reset_erased() {
        for (size_t i = 0; i < sizeof(data_); ++i) data_[i] = 0xFFu;
        for (size_t i = 0; i < sizeof(spare_); ++i) spare_[i] = 0xFFu;
        for (size_t i = 0; i < sizeof(cache_); ++i) cache_[i] = 0xFFu;
        for (size_t b = 0; b < Blocks; ++b) block_erases_[b] = 0u;
        wel_ = false;
        status_ = 0u;
        busy_remaining_ = 0u;
        inject_ecc_row_ = -1;
        inject_ecc_bits_ = 0u;
        fault_ = {};
        stats_ = {};
    }

    void set_jedec_id(uint8_t mfr, uint8_t dev_hi, uint8_t dev_lo) {
        jedec_id_[0] = mfr;
        jedec_id_[1] = dev_hi;
        jedec_id_[2] = dev_lo;
    }

    void mark_factory_bad(uint32_t block, uint32_t page = 0u) {
        if (block < Blocks && page < PagesPerBlock) {
            spare_[(size_t(block) * PagesPerBlock + page) * SpareBytes] = 0x00u;
        }
    }

    void inject_ecc_status(int32_t row, uint8_t ecc_bits) {
        inject_ecc_row_ = row;
        inject_ecc_bits_ = ecc_bits;
    }

    void restore_power() {
        fault_.power_lost = false;
        fault_.power_cut_after_ops = -1;
        wel_ = false;
        busy_remaining_ = 0u;
    }

    FaultConfig& faults() { return fault_; }
    const ChipTelemetry& telemetry() const { return stats_; }
    uint32_t block_erase_count(size_t b) const { return b < Blocks ? block_erases_[b] : 0u; }
    SpiBus spi_bus(uint32_t max_poll = 100000u) { return {this, &spi_transfer, max_poll}; }
};

// =============================================================================
// 3. ONFI 1.0 x8 Parallel NAND Emulator (Micron MT29F4G08ABA / TC58NVG2S0H)
// =============================================================================
template<size_t Blocks, size_t PagesPerBlock, size_t PageBytes, size_t SpareBytes = 64u>
class ParallelNandMock {
    static_assert(Blocks >= 3 && PagesPerBlock >= 1 && PageBytes >= 256 && (PageBytes % 256u == 0u) && SpareBytes >= 16u);
    static constexpr size_t kTotalPages = Blocks * PagesPerBlock;

    uint8_t data_[kTotalPages * PageBytes]{};
    uint8_t spare_[kTotalPages * SpareBytes]{};
    uint8_t prog_buf_[PageBytes + SpareBytes]{};
    uint32_t block_erases_[Blocks]{};
    uint8_t onfi_id_[5]{0x2Cu, 0xDCu, 0x90u, 0x95u, 0x56u}; // Micron MT29F4G08ABA
    uint8_t cmd_{};
    uint8_t addr_buf_[5]{};
    size_t addr_len_{};
    uint32_t cur_row_{};
    uint32_t cur_col_{};
    uint8_t status_reg_{0xE0u};
    uint32_t busy_remaining_{};
    FaultConfig fault_{};
    ChipTelemetry stats_{};

    static void write_cmd(void* ctx, uint8_t c) {
        auto* s = static_cast<ParallelNandMock*>(ctx);
        if (!s || s->fault_.power_lost) return;
        s->cmd_ = c;
        s->stats_.last_opcode = c;
        if (c == 0xFFu) { // Reset
            s->addr_len_ = 0u;
            s->status_reg_ = 0xE0u;
            s->busy_remaining_ = 0u;
        } else if (c == 0x00u || c == 0x80u || c == 0x60u || c == 0x90u) {
            s->addr_len_ = 0u;
            if (c == 0x80u) {
                for (size_t i = 0; i < sizeof(s->prog_buf_); ++i) s->prog_buf_[i] = 0xFFu;
            }
        } else if (c == 0x30u) { // Page Read confirm
            s->cur_col_ = uint32_t(s->addr_buf_[0]) | (uint32_t(s->addr_buf_[1]) << 8);
            s->cur_row_ = uint32_t(s->addr_buf_[2]) | (uint32_t(s->addr_buf_[3]) << 8) | (uint32_t(s->addr_buf_[4]) << 16);
            s->busy_remaining_ = s->fault_.busy_polls;
            ++s->stats_.read_ops;
        } else if (c == 0x10u) { // Page Program confirm
            if (s->cur_row_ >= kTotalPages) { s->status_reg_ = 0xE1u; return; }
            const uint32_t blk = s->cur_row_ / static_cast<uint32_t>(PagesPerBlock);
            if (static_cast<int32_t>(blk) == s->fault_.fail_program_block) {
                s->status_reg_ = 0xE1u; // Fail bit 0
                s->busy_remaining_ = s->fault_.busy_polls;
                return;
            }
            if (s->fault_.power_cut_after_ops == 0) {
                const size_t torn = PageBytes < s->fault_.torn_program_bytes ? PageBytes : s->fault_.torn_program_bytes;
                for (size_t i = 0; i < torn; ++i) s->data_[size_t(s->cur_row_) * PageBytes + i] &= s->prog_buf_[i];
                s->fault_.power_lost = true;
                s->status_reg_ = 0xE1u;
                ++s->stats_.torn_cuts;
                return;
            }
            if (s->fault_.power_cut_after_ops > 0) --s->fault_.power_cut_after_ops;

            for (size_t i = 0; i < PageBytes; ++i) s->data_[size_t(s->cur_row_) * PageBytes + i] &= s->prog_buf_[i];
            for (size_t i = 0; i < SpareBytes; ++i) s->spare_[size_t(s->cur_row_) * SpareBytes + i] &= s->prog_buf_[PageBytes + i];
            s->status_reg_ = 0xE0u;
            s->busy_remaining_ = s->fault_.busy_polls;
            ++s->stats_.program_ops;
        } else if (c == 0xD0u) { // Block Erase confirm
            const uint32_t row = uint32_t(s->addr_buf_[0]) | (uint32_t(s->addr_buf_[1]) << 8) | (uint32_t(s->addr_buf_[2]) << 16);
            const uint32_t blk = row / static_cast<uint32_t>(PagesPerBlock);
            if (blk >= Blocks || static_cast<int32_t>(blk) == s->fault_.fail_erase_block) {
                s->status_reg_ = 0xE1u;
                s->busy_remaining_ = s->fault_.busy_polls;
                return;
            }
            if (s->fault_.power_cut_after_ops == 0) {
                s->data_[size_t(blk) * PagesPerBlock * PageBytes] ^= 0x55u;
                s->fault_.power_lost = true;
                s->status_reg_ = 0xE1u;
                ++s->stats_.torn_cuts;
                return;
            }
            if (s->fault_.power_cut_after_ops > 0) --s->fault_.power_cut_after_ops;

            const size_t d_base = size_t(blk) * PagesPerBlock * PageBytes;
            const size_t s_base = size_t(blk) * PagesPerBlock * SpareBytes;
            for (size_t i = 0; i < PagesPerBlock * PageBytes; ++i) s->data_[d_base + i] = 0xFFu;
            for (size_t i = 0; i < PagesPerBlock * SpareBytes; ++i) s->spare_[s_base + i] = 0xFFu;
            ++s->block_erases_[blk];
            s->status_reg_ = 0xE0u;
            s->busy_remaining_ = s->fault_.busy_polls;
            ++s->stats_.erase_ops;
        }
    }

    static void write_addr(void* ctx, uint8_t a) {
        auto* s = static_cast<ParallelNandMock*>(ctx);
        if (!s || s->fault_.power_lost) return;
        if (s->addr_len_ < 5u) s->addr_buf_[s->addr_len_++] = a;
        if (s->cmd_ == 0x80u && s->addr_len_ == 5u) {
            s->cur_col_ = uint32_t(s->addr_buf_[0]) | (uint32_t(s->addr_buf_[1]) << 8);
            s->cur_row_ = uint32_t(s->addr_buf_[2]) | (uint32_t(s->addr_buf_[3]) << 8) | (uint32_t(s->addr_buf_[4]) << 16);
        }
    }

    static void write_data(void* ctx, const uint8_t* bytes, size_t len) {
        auto* s = static_cast<ParallelNandMock*>(ctx);
        if (!s || !bytes || s->fault_.power_lost) return;
        for (size_t i = 0; i < len && (size_t(s->cur_col_) + i) < sizeof(s->prog_buf_); ++i) {
            s->prog_buf_[s->cur_col_ + i] = bytes[i];
        }
        s->cur_col_ += static_cast<uint32_t>(len);
    }

    static void read_data(void* ctx, uint8_t* bytes, size_t len) {
        auto* s = static_cast<ParallelNandMock*>(ctx);
        if (!s || !bytes) return;
        if (s->fault_.power_lost) {
            for (size_t i = 0; i < len; ++i) bytes[i] = 0xFFu;
            return;
        }
        if (s->cmd_ == 0x70u) {
            for (size_t i = 0; i < len; ++i) bytes[i] = s->status_reg_;
            return;
        }
        if (s->cmd_ == 0x90u) {
            for (size_t i = 0; i < len; ++i) bytes[i] = (i < 5u) ? s->onfi_id_[i] : 0xFFu;
            return;
        }
        if (s->cur_row_ >= kTotalPages) {
            for (size_t i = 0; i < len; ++i) bytes[i] = 0xFFu;
            return;
        }
        for (size_t i = 0; i < len; ++i) {
            const uint32_t col = s->cur_col_ + static_cast<uint32_t>(i);
            if (col < PageBytes) {
                bytes[i] = s->data_[size_t(s->cur_row_) * PageBytes + col];
            } else if (col - PageBytes < SpareBytes) {
                bytes[i] = s->spare_[size_t(s->cur_row_) * SpareBytes + (col - PageBytes)];
            } else {
                bytes[i] = 0xFFu;
            }
        }
        s->cur_col_ += static_cast<uint32_t>(len);
    }

    static bool is_ready(void* ctx) {
        auto* s = static_cast<ParallelNandMock*>(ctx);
        if (!s) return false;
        if (s->fault_.power_lost) return true; // Let status read observe 0xE1 fail bit
        if (s->busy_remaining_ > 0u) {
            --s->busy_remaining_;
            ++s->stats_.busy_polls_served;
            return false;
        }
        return true;
    }

public:
    constexpr ParallelNandMock() = default;

    void reset_erased() {
        for (size_t i = 0; i < sizeof(data_); ++i) data_[i] = 0xFFu;
        for (size_t i = 0; i < sizeof(spare_); ++i) spare_[i] = 0xFFu;
        for (size_t i = 0; i < sizeof(prog_buf_); ++i) prog_buf_[i] = 0xFFu;
        for (size_t b = 0; b < Blocks; ++b) block_erases_[b] = 0u;
        cmd_ = 0u;
        addr_len_ = 0u;
        cur_row_ = cur_col_ = 0u;
        status_reg_ = 0xE0u;
        busy_remaining_ = 0u;
        fault_ = {};
        stats_ = {};
    }

    void mark_factory_bad(uint32_t block, uint32_t page = 0u) {
        if (block < Blocks && page < PagesPerBlock) {
            spare_[(size_t(block) * PagesPerBlock + page) * SpareBytes] = 0x00u;
        }
    }

    void flip_data_bit(uint32_t block, uint32_t page, size_t byte_off, uint8_t mask) {
        const size_t row = size_t(block) * PagesPerBlock + page;
        if (row < kTotalPages && byte_off < PageBytes) data_[row * PageBytes + byte_off] ^= mask;
    }

    void flip_spare_bit(uint32_t block, uint32_t page, size_t spare_off, uint8_t mask) {
        const size_t row = size_t(block) * PagesPerBlock + page;
        if (row < kTotalPages && spare_off < SpareBytes) spare_[row * SpareBytes + spare_off] ^= mask;
    }

    void restore_power() {
        fault_.power_lost = false;
        fault_.power_cut_after_ops = -1;
        status_reg_ = 0xE0u;
        busy_remaining_ = 0u;
    }

    FaultConfig& faults() { return fault_; }
    const ChipTelemetry& telemetry() const { return stats_; }
    uint32_t block_erase_count(size_t b) const { return b < Blocks ? block_erases_[b] : 0u; }

    ParallelNandBus bus(uint32_t max_poll = 100000u) {
        return {this, &write_cmd, &write_addr, &write_data, &read_data, &is_ready, max_poll};
    }
};

// =============================================================================
// 4. 16-Bit JEDEC/CFI Parallel NOR Emulator (Infineon S29GL128S / MT28EW128ABA)
// =============================================================================
template<size_t Blocks, size_t PagesPerBlock, size_t PageBytes>
class ParallelNorMock {
    static_assert(Blocks >= 3 && PagesPerBlock >= 1 && PageBytes >= 128 && (PageBytes % 2u == 0u));
    static constexpr size_t kWordsPerPage = PageBytes / 2u;
    static constexpr size_t kWordsPerBlock = PagesPerBlock * kWordsPerPage;
    static constexpr size_t kTotalWords = Blocks * kWordsPerBlock;

    uint16_t words_[kTotalWords]{};
    uint32_t block_erases_[Blocks]{};
    int state_{};
    bool cfi_mode_{};
    uint32_t busy_remaining_{};
    uint16_t busy_target_word_{0xFFFFu};
    uint32_t active_page_words_written_{};
    FaultConfig fault_{};
    ChipTelemetry stats_{};

    static void write16(void* ctx, uint32_t off, uint16_t val) {
        auto* s = static_cast<ParallelNorMock*>(ctx);
        if (!s || s->fault_.power_lost) return;

        if (val == 0x00F0u) { // Reset / Return to Read Array
            s->state_ = 0;
            s->cfi_mode_ = false;
            return;
        }
        if (s->state_ == 0 && off == 0x0055u && val == 0x0098u) { // CFI Query Entry
            s->cfi_mode_ = true;
            s->stats_.last_opcode = 0x98u;
            return;
        }
        if (s->state_ == 0 && off == 0x555u && val == 0x00AAu) { s->state_ = 1; return; }
        if (s->state_ == 1 && off == 0x2AAu && val == 0x0055u) { s->state_ = 2; return; }
        if (s->state_ == 2 && off == 0x555u && val == 0x00A0u) {
            s->state_ = 3;
            s->stats_.last_opcode = 0xA0u;
            return;
        }
        if (s->state_ == 3) { // Word Program (16-bit AND physics + DQ7 busy poll)
            s->state_ = 0;
            if (off >= kTotalWords) return;
            const uint32_t blk = off / static_cast<uint32_t>(kWordsPerBlock);
            if (static_cast<int32_t>(blk) == s->fault_.fail_program_block) {
                s->busy_remaining_ = 0xFFFFFFFFu; // Stuck DQ7 -> timeout
                s->busy_target_word_ = val;
                return;
            }
            if ((off % kWordsPerPage) == 0u) {
                if (s->fault_.power_cut_after_ops == 0) {
                    s->words_[off] &= val;
                    s->fault_.power_lost = true;
                    ++s->stats_.torn_cuts;
                    return;
                }
                if (s->fault_.power_cut_after_ops > 0) --s->fault_.power_cut_after_ops;
                ++s->stats_.program_ops;
            }
            s->words_[off] &= val;
            s->busy_target_word_ = s->words_[off];
            s->busy_remaining_ = s->fault_.busy_polls;
            return;
        }
        if (s->state_ == 2 && off == 0x555u && val == 0x0080u) { s->state_ = 4; return; }
        if (s->state_ == 4 && off == 0x555u && val == 0x00AAu) { s->state_ = 5; return; }
        if (s->state_ == 5 && off == 0x2AAu && val == 0x0055u) { s->state_ = 6; return; }
        if (s->state_ == 6 && val == 0x0030u) { // Sector Erase
            s->state_ = 0;
            s->stats_.last_opcode = 0x30u;
            const uint32_t blk = off / static_cast<uint32_t>(kWordsPerBlock);
            if (blk >= Blocks) return;
            if (static_cast<int32_t>(blk) == s->fault_.fail_erase_block) {
                s->busy_remaining_ = 0xFFFFFFFFu;
                s->busy_target_word_ = 0xFFFFu;
                return;
            }
            if (s->fault_.power_cut_after_ops == 0) {
                s->words_[blk * kWordsPerBlock] ^= 0x5555u;
                s->fault_.power_lost = true;
                ++s->stats_.torn_cuts;
                return;
            }
            if (s->fault_.power_cut_after_ops > 0) --s->fault_.power_cut_after_ops;

            const size_t base = size_t(blk) * kWordsPerBlock;
            for (size_t i = 0; i < kWordsPerBlock; ++i) s->words_[base + i] = 0xFFFFu;
            ++s->block_erases_[blk];
            s->busy_target_word_ = 0xFFFFu;
            s->busy_remaining_ = s->fault_.busy_polls;
            ++s->stats_.erase_ops;
            return;
        }
        s->state_ = 0;
    }

    static uint16_t read16(void* ctx, uint32_t off) {
        auto* s = static_cast<ParallelNorMock*>(ctx);
        if (!s) return 0xFFFFu;
        if (s->fault_.power_lost) {
            return static_cast<uint16_t>(((s->busy_target_word_ ^ 0x0080u) & 0x0080u) | 0x0020u);
        }
        if (s->busy_remaining_ > 0u) {
            const bool faulted = (s->busy_remaining_ == 0xFFFFFFFFu);
            if (!faulted) --s->busy_remaining_;
            ++s->stats_.busy_polls_served;
            // JEDEC Data# Polling: DQ7 = ~target_DQ7 while busy; DQ5 = 1 only on hardware timeout/fail
            const uint16_t dq7 = static_cast<uint16_t>((s->busy_target_word_ ^ 0x0080u) & 0x0080u);
            return static_cast<uint16_t>(dq7 | (faulted ? 0x0020u : 0x0000u));
        }
        if (s->cfi_mode_) {
            if (off == 0x10u) return 'Q';
            if (off == 0x11u) return 'R';
            if (off == 0x12u) return 'Y';
            return 0x0000u;
        }
        if ((off % kWordsPerPage) == 0u) ++s->stats_.read_ops;
        return off < kTotalWords ? s->words_[off] : 0xFFFFu;
    }

public:
    constexpr ParallelNorMock() = default;

    void reset_erased() {
        for (size_t i = 0; i < kTotalWords; ++i) words_[i] = 0xFFFFu;
        for (size_t b = 0; b < Blocks; ++b) block_erases_[b] = 0u;
        state_ = 0;
        cfi_mode_ = false;
        busy_remaining_ = 0u;
        busy_target_word_ = 0xFFFFu;
        active_page_words_written_ = 0u;
        fault_ = {};
        stats_ = {};
    }

    void restore_power() {
        fault_.power_lost = false;
        fault_.power_cut_after_ops = -1;
        state_ = 0;
        cfi_mode_ = false;
        busy_remaining_ = 0u;
    }

    void flip_bit(uint32_t block, uint32_t page, size_t word_off, uint16_t mask) {
        const size_t idx = (size_t(block) * PagesPerBlock + page) * kWordsPerPage + word_off;
        if (idx < kTotalWords) words_[idx] ^= mask;
    }

    FaultConfig& faults() { return fault_; }
    const ChipTelemetry& telemetry() const { return stats_; }
    uint32_t block_erase_count(size_t b) const { return b < Blocks ? block_erases_[b] : 0u; }

    ParallelNorBus bus(uint32_t max_poll = 200000u) {
        return {this, &write16, &read16, max_poll};
    }
};

} // namespace rtos::storage::mocks
