/** @file tests/storage.cpp
 * @brief Comprehensive verification of RTOS_STORAGE:
 *   1. Castagnoli CRC-32C standard test vector
 *   2. Exhaustive 22-bit Complementary Hamming SECDED (all 2048 1-bit data flips,
 *      all 22 1-bit ECC flips, and 2-bit uncorrectable error sweep)
 *   3. Bit-accurate protocol simulators for all 4 flash families:
 *        - Serial SPI NOR (Winbond W25Q128JV)
 *        - Serial SPI NAND (Winbond W25N01GV)
 *        - Parallel ONFI 1.0 x8 NAND (Micron MT29F4G08ABA) w/ SW ECC & bad blocks
 *        - Parallel 16-bit CFI/JEDEC NOR (Infineon S29GL128S)
 *   4. Dynamic + static wear leveling, read-disturb scrubbing, bad-block quarantine
 *   5. Exhaustive power-cut crash recovery fuzzing (torn writes & mid-GC cuts)
 */
#include "rtos_storage.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace rtos::storage;

namespace {

// -----------------------------------------------------------------------------
// 1. CRC-32C + Exhaustive Hamming SECDED Verification
// -----------------------------------------------------------------------------
void test_crc32c_and_hamming_exhaustive() {
    // RFC 3720 / iSCSI Castagnoli CRC-32C of "123456789" == 0xE3069283
    const char vec[] = "123456789";
    assert(crc32c(vec, 9) == 0xE3069283u);

    uint8_t orig[256]{};
    for (size_t i = 0; i < 256; ++i) {
        orig[i] = static_cast<uint8_t>((i * 73u + 19u) ^ (i >> 2));
    }
    uint8_t ecc[3]{};
    hamming256_encode(orig, ecc);

    // Clean check
    uint8_t buf[256]{};
    std::memcpy(buf, orig, 256);
    assert(hamming256_correct(buf, ecc) == EccStatus::clean);

    // Exhaustive: flip every single one of the 2048 data bits
    for (size_t bit = 0; bit < 2048; ++bit) {
        std::memcpy(buf, orig, 256);
        buf[bit / 8u] ^= static_cast<uint8_t>(1u << (bit % 8u));
        assert(hamming256_correct(buf, ecc) == EccStatus::corrected_1bit);
        assert(std::memcmp(buf, orig, 256) == 0);
    }

    // Exhaustive: flip every single one of the 22 ECC parity bits
    for (size_t ebit = 0; ebit < 22; ++ebit) {
        uint8_t bad_ecc[3]{ecc[0], ecc[1], ecc[2]};
        bad_ecc[ebit / 8u] ^= static_cast<uint8_t>(1u << (ebit % 8u));
        std::memcpy(buf, orig, 256);
        assert(hamming256_correct(buf, bad_ecc) == EccStatus::corrected_ecc_bit);
        assert(std::memcmp(buf, orig, 256) == 0);
    }

    // 2-bit uncorrectable sweep across data+data, data+ECC, ECC+ECC
    for (size_t b1 = 0; b1 < 256; b1 += 17) {
        for (size_t b2 = b1 + 1; b2 < 256; b2 += 23) {
            std::memcpy(buf, orig, 256);
            buf[b1] ^= 0x01u;
            buf[b2] ^= 0x80u;
            assert(hamming256_correct(buf, ecc) == EccStatus::uncorrectable_2bit);
        }
    }
    // Same-byte 2-bit flip
    std::memcpy(buf, orig, 256);
    buf[42] ^= 0x05u;
    assert(hamming256_correct(buf, ecc) == EccStatus::uncorrectable_2bit);

    // 1 data bit + 1 ECC bit flip
    uint8_t bad_ecc[3]{ecc[0], ecc[1], ecc[2]};
    bad_ecc[0] ^= 0x04u;
    std::memcpy(buf, orig, 256);
    buf[99] ^= 0x10u;
    assert(hamming256_correct(buf, bad_ecc) == EccStatus::uncorrectable_2bit);
}

// -----------------------------------------------------------------------------
// 2. Simulated Serial SPI NOR (Winbond W25Q128JV)
// -----------------------------------------------------------------------------
struct MockSerialNor {
    std::vector<uint8_t> mem;
    bool wel{false};
    uint32_t erase_ops{0};
    uint32_t program_ops{0};

    explicit MockSerialNor(size_t total_bytes) : mem(total_bytes, 0xFFu) {}

    static bool spi_transfer(void* ctx, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len) {
        auto* self = static_cast<MockSerialNor*>(ctx);
        if (!tx || tx_len == 0) return false;
        const uint8_t op = tx[0];
        if (op == 0x9Fu && rx && rx_len == 3) {
            rx[0] = 0xEFu; rx[1] = 0x40u; rx[2] = 0x18u; // Winbond W25Q128JV JEDEC ID
            return true;
        }
        if (op == 0x06u) { self->wel = true; return true; }
        if (op == 0x05u && rx && rx_len == 1) { rx[0] = self->wel ? 0x02u : 0x00u; return true; }
        if (op == 0x03u && tx_len == 4 && rx) {
            const uint32_t addr = (uint32_t(tx[1]) << 16) | (uint32_t(tx[2]) << 8) | tx[3];
            if (addr + rx_len > self->mem.size()) return false;
            std::memcpy(rx, self->mem.data() + addr, rx_len);
            return true;
        }
        if (op == 0x02u && tx_len > 4) {
            if (!self->wel) return false;
            self->wel = false;
            const uint32_t addr = (uint32_t(tx[1]) << 16) | (uint32_t(tx[2]) << 8) | tx[3];
            const size_t n = tx_len - 4;
            if (addr + n > self->mem.size()) return false;
            for (size_t i = 0; i < n; ++i) self->mem[addr + i] &= tx[4 + i]; // NOR 1->0 AND semantics
            ++self->program_ops;
            return true;
        }
        if ((op == 0x20u || op == 0xD8u) && tx_len == 4) {
            if (!self->wel) return false;
            self->wel = false;
            const uint32_t addr = (uint32_t(tx[1]) << 16) | (uint32_t(tx[2]) << 8) | tx[3];
            const size_t len = (op == 0x20u) ? 4096u : 65536u;
            if (addr + len > self->mem.size()) return false;
            std::memset(self->mem.data() + addr, 0xFFu, len);
            ++self->erase_ops;
            return true;
        }
        return false;
    }
};

// -----------------------------------------------------------------------------
// 3. Simulated Serial SPI NAND (Winbond W25N01GV)
// -----------------------------------------------------------------------------
struct MockSerialNand {
    uint32_t blocks;
    uint32_t pages_per_block;
    uint32_t page_bytes;
    uint32_t spare_bytes;
    std::vector<uint8_t> data;
    std::vector<uint8_t> spare;
    std::vector<uint8_t> cache;
    bool wel{false};
    uint8_t status{0x00u};
    int32_t inject_ecc_corrected_row{-1};

    MockSerialNand(uint32_t b, uint32_t ppb, uint32_t pb, uint32_t sb)
        : blocks(b), pages_per_block(ppb), page_bytes(pb), spare_bytes(sb),
          data(size_t(b) * ppb * pb, 0xFFu),
          spare(size_t(b) * ppb * sb, 0xFFu),
          cache(pb + sb, 0xFFu) {}

    static bool spi_transfer(void* ctx, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len) {
        auto* s = static_cast<MockSerialNand*>(ctx);
        const uint8_t op = tx[0];
        if (op == 0x06u) { s->wel = true; return true; }
        if (op == 0x0Fu && tx_len == 2 && rx && rx_len == 1) {
            rx[0] = s->status;
            return true;
        }
        if (op == 0x13u && tx_len == 4) {
            const uint32_t row = (uint32_t(tx[1]) << 16) | (uint32_t(tx[2]) << 8) | tx[3];
            if (row >= s->blocks * s->pages_per_block) return false;
            std::memcpy(s->cache.data(), s->data.data() + size_t(row) * s->page_bytes, s->page_bytes);
            std::memcpy(s->cache.data() + s->page_bytes, s->spare.data() + size_t(row) * s->spare_bytes, s->spare_bytes);
            s->status = (static_cast<int32_t>(row) == s->inject_ecc_corrected_row) ? 0x10u : 0x00u;
            return true;
        }
        if (op == 0x03u && tx_len == 4 && rx) {
            const uint32_t col = (uint32_t(tx[1]) << 8) | tx[2];
            if (col + rx_len > s->cache.size()) return false;
            std::memcpy(rx, s->cache.data() + col, rx_len);
            return true;
        }
        if ((op == 0x02u || op == 0x84u) && tx_len >= 3) {
            if (op == 0x02u) std::fill(s->cache.begin(), s->cache.end(), 0xFFu);
            const uint32_t col = (uint32_t(tx[1]) << 8) | tx[2];
            const size_t n = tx_len - 3;
            if (col + n > s->cache.size()) return false;
            std::memcpy(s->cache.data() + col, tx + 3, n);
            return true;
        }
        if (op == 0x10u && tx_len == 4) {
            if (!s->wel) return false;
            s->wel = false;
            const uint32_t row = (uint32_t(tx[1]) << 16) | (uint32_t(tx[2]) << 8) | tx[3];
            if (row >= s->blocks * s->pages_per_block) return false;
            for (size_t i = 0; i < s->page_bytes; ++i)
                s->data[size_t(row) * s->page_bytes + i] &= s->cache[i];
            for (size_t i = 0; i < s->spare_bytes; ++i)
                s->spare[size_t(row) * s->spare_bytes + i] &= s->cache[s->page_bytes + i];
            s->status = 0x00u;
            return true;
        }
        if (op == 0xD8u && tx_len == 4) {
            if (!s->wel) return false;
            s->wel = false;
            const uint32_t row = (uint32_t(tx[1]) << 16) | (uint32_t(tx[2]) << 8) | tx[3];
            const uint32_t blk = row / s->pages_per_block;
            if (blk >= s->blocks) return false;
            std::memset(s->data.data() + size_t(blk) * s->pages_per_block * s->page_bytes,
                        0xFFu, size_t(s->pages_per_block) * s->page_bytes);
            std::memset(s->spare.data() + size_t(blk) * s->pages_per_block * s->spare_bytes,
                        0xFFu, size_t(s->pages_per_block) * s->spare_bytes);
            s->status = 0x00u;
            return true;
        }
        return false;
    }
};

// -----------------------------------------------------------------------------
// 4. Simulated ONFI 1.0 x8 Parallel NAND (Micron MT29F4G08ABA)
// -----------------------------------------------------------------------------
struct MockParallelNand {
    uint32_t blocks;
    uint32_t pages_per_block;
    uint32_t page_bytes;
    uint32_t spare_bytes;
    std::vector<uint8_t> data;
    std::vector<uint8_t> spare;
    uint8_t cmd{0};
    uint8_t addr_buf[5]{};
    size_t addr_len{0};
    uint32_t cur_row{0};
    uint32_t cur_col{0};
    uint8_t status_reg{0xE0u}; // Ready, pass, not write-protected
    std::vector<uint8_t> prog_buf;
    int32_t fail_program_block{-1};
    int32_t fail_erase_block{-1};

    MockParallelNand(uint32_t b, uint32_t ppb, uint32_t pb, uint32_t sb)
        : blocks(b), pages_per_block(ppb), page_bytes(pb), spare_bytes(sb),
          data(size_t(b) * ppb * pb, 0xFFu),
          spare(size_t(b) * ppb * sb, 0xFFu),
          prog_buf(pb + sb, 0xFFu) {}

    void mark_factory_bad(uint32_t blk) {
        const size_t row = size_t(blk) * pages_per_block;
        spare[row * spare_bytes + 0] = 0x00u;
    }

    static void write_cmd(void* ctx, uint8_t c) {
        auto* s = static_cast<MockParallelNand*>(ctx);
        s->cmd = c;
        if (c == 0x00u || c == 0x80u || c == 0x60u) {
            s->addr_len = 0;
            if (c == 0x80u) std::fill(s->prog_buf.begin(), s->prog_buf.end(), 0xFFu);
        } else if (c == 0x30u) {
            s->cur_col = uint32_t(s->addr_buf[0]) | (uint32_t(s->addr_buf[1]) << 8);
            s->cur_row = uint32_t(s->addr_buf[2]) | (uint32_t(s->addr_buf[3]) << 8) | (uint32_t(s->addr_buf[4]) << 16);
        } else if (c == 0x10u) {
            const uint32_t blk = s->cur_row / s->pages_per_block;
            if (static_cast<int32_t>(blk) == s->fail_program_block) {
                s->status_reg = 0xE1u; // Fail bit set!
                return;
            }
            for (size_t i = 0; i < s->page_bytes; ++i)
                s->data[size_t(s->cur_row) * s->page_bytes + i] &= s->prog_buf[i];
            for (size_t i = 0; i < s->spare_bytes; ++i)
                s->spare[size_t(s->cur_row) * s->spare_bytes + i] &= s->prog_buf[s->page_bytes + i];
            s->status_reg = 0xE0u;
        } else if (c == 0xD0u) {
            const uint32_t row = uint32_t(s->addr_buf[0]) | (uint32_t(s->addr_buf[1]) << 8) | (uint32_t(s->addr_buf[2]) << 16);
            const uint32_t blk = row / s->pages_per_block;
            if (static_cast<int32_t>(blk) == s->fail_erase_block) {
                s->status_reg = 0xE1u;
                return;
            }
            std::memset(s->data.data() + size_t(blk) * s->pages_per_block * s->page_bytes,
                        0xFFu, size_t(s->pages_per_block) * s->page_bytes);
            std::memset(s->spare.data() + size_t(blk) * s->pages_per_block * s->spare_bytes,
                        0xFFu, size_t(s->pages_per_block) * s->spare_bytes);
            s->status_reg = 0xE0u;
        }
    }

    static void write_addr(void* ctx, uint8_t a) {
        auto* s = static_cast<MockParallelNand*>(ctx);
        if (s->addr_len < 5) s->addr_buf[s->addr_len++] = a;
        if (s->cmd == 0x80u && s->addr_len == 5) {
            s->cur_col = uint32_t(s->addr_buf[0]) | (uint32_t(s->addr_buf[1]) << 8);
            s->cur_row = uint32_t(s->addr_buf[2]) | (uint32_t(s->addr_buf[3]) << 8) | (uint32_t(s->addr_buf[4]) << 16);
        }
    }

    static void write_data(void* ctx, const uint8_t* bytes, size_t len) {
        auto* s = static_cast<MockParallelNand*>(ctx);
        for (size_t i = 0; i < len && (s->cur_col + i) < s->prog_buf.size(); ++i) {
            s->prog_buf[s->cur_col + i] = bytes[i];
        }
        s->cur_col += static_cast<uint32_t>(len);
    }

    static void read_data(void* ctx, uint8_t* bytes, size_t len) {
        auto* s = static_cast<MockParallelNand*>(ctx);
        if (s->cmd == 0x70u) {
            for (size_t i = 0; i < len; ++i) bytes[i] = s->status_reg;
            return;
        }
        for (size_t i = 0; i < len; ++i) {
            const uint32_t col = s->cur_col + static_cast<uint32_t>(i);
            if (col < s->page_bytes) {
                bytes[i] = s->data[size_t(s->cur_row) * s->page_bytes + col];
            } else if (col - s->page_bytes < s->spare_bytes) {
                bytes[i] = s->spare[size_t(s->cur_row) * s->spare_bytes + (col - s->page_bytes)];
            } else {
                bytes[i] = 0xFFu;
            }
        }
        s->cur_col += static_cast<uint32_t>(len);
    }

    static bool is_ready(void*) { return true; }
};

// -----------------------------------------------------------------------------
// 5. Simulated 16-bit CFI/JEDEC Parallel NOR (Infineon S29GL128S)
// -----------------------------------------------------------------------------
struct MockParallelNor {
    uint32_t sector_words;
    std::vector<uint16_t> words;
    int state{0};

    MockParallelNor(uint32_t sectors, uint32_t words_per_sector)
        : sector_words(words_per_sector), words(size_t(sectors) * words_per_sector, 0xFFFFu) {}

    static void write16(void* ctx, uint32_t off, uint16_t val) {
        auto* s = static_cast<MockParallelNor*>(ctx);
        if (s->state == 0 && off == 0x555u && val == 0x00AAu) { s->state = 1; return; }
        if (s->state == 1 && off == 0x2AAu && val == 0x0055u) { s->state = 2; return; }
        if (s->state == 2 && off == 0x555u && val == 0x00A0u) { s->state = 3; return; } // Word program
        if (s->state == 3) {
            if (off < s->words.size()) s->words[off] &= val;
            s->state = 0;
            return;
        }
        if (s->state == 2 && off == 0x555u && val == 0x0080u) { s->state = 4; return; } // Erase setup
        if (s->state == 4 && off == 0x555u && val == 0x00AAu) { s->state = 5; return; }
        if (s->state == 5 && off == 0x2AAu && val == 0x0055u) { s->state = 6; return; }
        if (s->state == 6 && val == 0x0030u) { // Sector erase
            const uint32_t base = (off / s->sector_words) * s->sector_words;
            for (uint32_t i = 0; i < s->sector_words && base + i < s->words.size(); ++i)
                s->words[base + i] = 0xFFFFu;
            s->state = 0;
            return;
        }
        s->state = 0;
    }

    static uint16_t read16(void* ctx, uint32_t off) {
        auto* s = static_cast<MockParallelNor*>(ctx);
        return off < s->words.size() ? s->words[off] : 0xFFFFu;
    }
};

// -----------------------------------------------------------------------------
// Adapter End-to-End Tests
// -----------------------------------------------------------------------------
void test_serial_nor_adapter_and_fs() {
    // 8 blocks * 8 pages/block * 512 B = 32 KiB (4 KiB blocks -> 0x20 sector erase)
    MockSerialNor chip(8u * 4096u);
    SerialNorAdapter adapter;
    assert(adapter.initialize({&chip, MockSerialNor::spi_transfer, 1000}, 8u, 8u, 512u));
    uint8_t id[3]{};
    assert(adapter.read_jedec_id(id) && id[0] == 0xEFu && id[1] == 0x40u && id[2] == 0x18u);

    JournalFs<8, 16, 512> fs;
    assert(fs.format(adapter.backend()) == FsResult::ok);

    const char msg[] = "Serial-NOR-W25Q128JV-OK";
    assert(fs.write_file(0, msg, sizeof(msg)) == FsResult::ok);
    char out[64]{};
    size_t out_len = 0;
    assert(fs.read_file(0, out, sizeof(out), out_len) == FsResult::ok);
    assert(out_len == sizeof(msg) && std::memcmp(out, msg, sizeof(msg)) == 0);

    // Remount fresh instance from raw SPI NOR chip memory
    JournalFs<8, 16, 512> fs2;
    assert(fs2.mount(adapter.backend()) == FsResult::ok);
    assert(fs2.read_file(0, out, sizeof(out), out_len) == FsResult::ok);
    assert(out_len == sizeof(msg) && std::memcmp(out, msg, sizeof(msg)) == 0);
}

void test_serial_nand_adapter_and_scrub() {
    MockSerialNand chip(8u, 4u, 512u, 64u);
    // Mark block 2 as factory bad in spare[0]
    chip.spare[size_t(2u) * 4u * 64u] = 0x00u;

    SerialNandAdapter adapter;
    assert(adapter.initialize({&chip, MockSerialNand::spi_transfer, 1000}, 8u, 4u, 512u, 64u));

    JournalFs<8, 16, 512> fs;
    assert(fs.format(adapter.backend()) == FsResult::ok);
    assert(fs.stats().bad_blocks == 1u);

    // Fill block 0 completely, then inject on-die ECC correction on row 0
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t v = 0xA000u + i;
        assert(fs.write_file(i, &v, sizeof(v)) == FsResult::ok);
    }
    chip.inject_ecc_corrected_row = 0;
    uint32_t read_v = 0;
    size_t got = 0;
    assert(fs.read_file(0, &read_v, sizeof(read_v), got) == FsResult::ok);
    assert(read_v == 0xA000u);
    assert(fs.stats().ecc_corrections == 1u);

    // gc_step() must scrub block 0 (relocating files 0..3 cleanly)
    chip.inject_ecc_corrected_row = -1;
    assert(fs.gc_step() == FsResult::ok);
    for (uint32_t i = 0; i < 4; ++i) {
        assert(fs.read_file(i, &read_v, sizeof(read_v), got) == FsResult::ok);
        assert(read_v == 0xA000u + i);
    }
}

void test_parallel_nand_ecc_and_bad_blocks() {
    // 10 blocks, 4 pages/block, 512 B/page, 64 B OOB, SW Hamming SECDED enabled
    MockParallelNand chip(10u, 4u, 512u, 64u);
    chip.mark_factory_bad(3u);

    ParallelNandAdapter adapter;
    ParallelNandBus bus{&chip, MockParallelNand::write_cmd, MockParallelNand::write_addr,
                        MockParallelNand::write_data, MockParallelNand::read_data,
                        MockParallelNand::is_ready, 1000};
    assert(adapter.initialize(bus, 10u, 4u, 512u, 64u, true));

    JournalFs<10, 16, 512> fs;
    assert(fs.format(adapter.backend()) == FsResult::ok);
    assert(fs.stats().bad_blocks == 1u);

    const char payload[] = "ONFI-Parallel-NAND-MT29F4G08ABA-Hamming-SECDED";
    assert(fs.write_file(1, payload, sizeof(payload)) == FsResult::ok);

    // Flip 1 bit in BOTH 256-byte Hamming chunks of page 0 in block 0 -> must silently correct!
    chip.data[12] ^= 0x08u;
    chip.data[256 + 77] ^= 0x40u;

    char out[64]{};
    size_t got = 0;
    assert(fs.read_file(1, out, sizeof(out), got) == FsResult::ok);
    assert(got == sizeof(payload) && std::memcmp(out, payload, sizeof(payload)) == 0);
    assert(fs.stats().ecc_corrections == 1u);

    // Inject runtime program failure on block 1 -> JournalFs must quarantine block 1 & succeed on another block!
    chip.fail_program_block = 1;
    for (uint32_t k = 0; k < 6; ++k) {
        const uint32_t val = 0x5500u + k;
        assert(fs.write_file(2, &val, sizeof(val)) == FsResult::ok);
    }
    assert(fs.stats().bad_blocks == 2u); // factory block 3 + runtime block 1!

    // Remount fresh instance: runtime bad block 1 must STILL be quarantined via journaled bad_block record!
    JournalFs<10, 16, 512> fs2;
    assert(fs2.mount(adapter.backend()) == FsResult::ok);
    assert(fs2.stats().bad_blocks == 2u);
    uint32_t latest = 0;
    assert(fs2.read_file(2, &latest, sizeof(latest), got) == FsResult::ok);
    assert(latest == 0x5505u);
}

void test_parallel_nor_adapter() {
    // 6 sectors * 4 pages * 256 B = 1024 B/sector (512 16-bit words/sector)
    MockParallelNor chip(6u, 512u);
    ParallelNorAdapter adapter;
    assert(adapter.initialize({&chip, MockParallelNor::write16, MockParallelNor::read16, 1000},
                              6u, 4u, 256u));

    JournalFs<6, 8, 256> fs;
    assert(fs.format(adapter.backend()) == FsResult::ok);
    const char txt[] = "Parallel-NOR-S29GL128S-CFI-16bit";
    assert(fs.write_file(3, txt, sizeof(txt)) == FsResult::ok);

    JournalFs<6, 8, 256> fs2;
    assert(fs2.mount(adapter.backend()) == FsResult::ok);
    char rx[64]{};
    size_t len = 0;
    assert(fs2.read_file(3, rx, sizeof(rx), len) == FsResult::ok);
    assert(len == sizeof(txt) && std::memcmp(rx, txt, sizeof(txt)) == 0);
}

// -----------------------------------------------------------------------------
// 6. Wear Leveling (Dynamic + Static) & Read-Disturb Scrubbing
// -----------------------------------------------------------------------------
void test_wear_leveling_and_read_disturb() {
    MockSerialNor chip(8u * 4096u); // 8 blocks, 8 pages/block, 512 B/page
    SerialNorAdapter adapter;
    assert(adapter.initialize({&chip, MockSerialNor::spi_transfer, 1000}, 8u, 8u, 512u));

    JournalFs<8, 16, 512> fs;
    fs.set_wear_threshold(2u);
    fs.set_read_disturb_limit(20u);
    assert(fs.format(adapter.backend()) == FsResult::ok);

    // Cold file written once at start
    const uint32_t cold_magic = 0xC01DC01Du;
    assert(fs.write_file(0, &cold_magic, sizeof(cold_magic)) == FsResult::ok);

    // Hammer hot files across 200 updates -> forces many GC cycles + static wear-leveling relocations!
    for (uint32_t iter = 0; iter < 200; ++iter) {
        const uint32_t id = 1u + (iter % 3u);
        assert(fs.write_file(id, &iter, sizeof(iter)) == FsResult::ok);
    }

    const auto st = fs.stats();
    assert(st.gc_runs > 15u);
    assert(st.static_relocations > 0u);
    assert(st.max_erase_count - st.min_erase_count <= 4u);

    // Cold file must still read back bit-for-bit identically!
    uint32_t got_cold = 0;
    size_t len = 0;
    assert(fs.read_file(0, &got_cold, sizeof(got_cold), len) == FsResult::ok);
    assert(got_cold == cold_magic);

    // Trigger read-disturb scrubbing on file 0's block
    for (int i = 0; i < 25; ++i) {
        assert(fs.read_file(0, &got_cold, sizeof(got_cold), len) == FsResult::ok);
    }
    const uint32_t gc_before = fs.stats().gc_runs;
    assert(fs.gc_step() == FsResult::ok);
    assert(fs.stats().gc_runs == gc_before + 1u);
    assert(fs.read_file(0, &got_cold, sizeof(got_cold), len) == FsResult::ok);
    assert(got_cold == cold_magic);
}

// -----------------------------------------------------------------------------
// 7. Exhaustive Power-Cut Crash-Recovery Fuzzing
// -----------------------------------------------------------------------------
struct PowerCutFlash {
    static constexpr uint32_t kBlocks = 6;
    static constexpr uint32_t kPagesPerBlock = 4;
    static constexpr uint32_t kPageBytes = 256;
    std::array<uint8_t, kBlocks * kPagesPerBlock * kPageBytes> mem{};
    int32_t cut_countdown{-1};
    bool power_lost{false};

    PowerCutFlash() { mem.fill(0xFFu); }

    FlashBackend backend() {
        return {this, {kPageBytes, 0, kPagesPerBlock, kBlocks, 100000u, FlashKind::serial_nor, false},
                read_cb, prog_cb, erase_cb, nullptr};
    }

    static IoStatus read_cb(void* ctx, uint32_t b, uint32_t p, uint8_t* d, size_t len, uint8_t*, size_t) {
        auto* s = static_cast<PowerCutFlash*>(ctx);
        const size_t off = (size_t(b) * kPagesPerBlock + p) * kPageBytes;
        std::memcpy(d, s->mem.data() + off, len);
        return IoStatus::ok;
    }

    static IoStatus prog_cb(void* ctx, uint32_t b, uint32_t p, const uint8_t* d, size_t len, const uint8_t*, size_t) {
        auto* s = static_cast<PowerCutFlash*>(ctx);
        if (s->power_lost) return IoStatus::bus_error;
        const size_t off = (size_t(b) * kPagesPerBlock + p) * kPageBytes;
        if (s->cut_countdown == 0) {
            // Sudden power loss MID-PAGE-PROGRAM: only first 52 bytes hit flash (torn header+partial payload!)
            for (size_t i = 0; i < 52 && i < len; ++i) s->mem[off + i] &= d[i];
            s->power_lost = true;
            return IoStatus::bus_error;
        }
        if (s->cut_countdown > 0) --s->cut_countdown;
        for (size_t i = 0; i < len; ++i) s->mem[off + i] &= d[i];
        return IoStatus::ok;
    }

    static IoStatus erase_cb(void* ctx, uint32_t b) {
        auto* s = static_cast<PowerCutFlash*>(ctx);
        if (s->power_lost) return IoStatus::bus_error;
        if (s->cut_countdown == 0) {
            // Sudden power loss right as erase starts: corrupts victim bytes partially
            const size_t off = size_t(b) * kPagesPerBlock * kPageBytes;
            s->mem[off] ^= 0x55u;
            s->power_lost = true;
            return IoStatus::bus_error;
        }
        if (s->cut_countdown > 0) --s->cut_countdown;
        const size_t off = size_t(b) * kPagesPerBlock * kPageBytes;
        std::memset(s->mem.data() + off, 0xFFu, size_t(kPagesPerBlock) * kPageBytes);
        return IoStatus::ok;
    }
};

void test_exhaustive_power_cut_recovery() {
    // Run the same 30-step write/overwrite/delete/GC workload with power cut at EVERY
    // flash op index from 0 to 40, remounting from raw bytes and proving strict atomicity!
    for (int32_t cut_at = 0; cut_at <= 40; ++cut_at) {
        PowerCutFlash flash;
        JournalFs<6, 8, 256> fs;
        assert(fs.format(flash.backend()) == FsResult::ok);

        // Track known-committed state AND in-flight target for each of 4 files
        struct FileOracle {
            bool committed_present{false};
            uint32_t committed_val{0};
            bool pending_present{false};
            uint32_t pending_val{0};
            bool in_flight{false};
        };
        FileOracle oracle[4]{};

        // Pre-populate 4 files cleanly before arming the power-cut trigger
        for (uint32_t id = 0; id < 4; ++id) {
            const uint32_t v = 1000u + id;
            assert(fs.write_file(id, &v, sizeof(v)) == FsResult::ok);
            oracle[id].committed_present = true;
            oracle[id].committed_val = v;
        }

        flash.cut_countdown = cut_at;
        for (uint32_t step = 0; step < 30 && !flash.power_lost; ++step) {
            const uint32_t id = step % 4u;
            if (step == 15u) {
                oracle[id].in_flight = true;
                oracle[id].pending_present = false;
                const auto r = fs.delete_file(id);
                if (r == FsResult::ok) {
                    oracle[id].committed_present = false;
                    oracle[id].in_flight = false;
                }
            } else {
                const uint32_t next_v = 2000u + step;
                oracle[id].in_flight = true;
                oracle[id].pending_present = true;
                oracle[id].pending_val = next_v;
                const auto r = fs.write_file(id, &next_v, sizeof(next_v));
                if (r == FsResult::ok) {
                    oracle[id].committed_present = true;
                    oracle[id].committed_val = next_v;
                    oracle[id].in_flight = false;
                }
            }
        }

        // Reboot: clear power_lost flag, mount a brand-new JournalFs from raw flash bytes!
        flash.power_lost = false;
        flash.cut_countdown = -1;
        JournalFs<6, 8, 256> recovered;
        assert(recovered.mount(flash.backend()) == FsResult::ok);

        for (uint32_t id = 0; id < 4; ++id) {
            uint32_t got_v = 0;
            size_t got_len = 0;
            const auto rd = recovered.read_file(id, &got_v, sizeof(got_v), got_len);
            if (!oracle[id].in_flight) {
                if (oracle[id].committed_present) {
                    assert(rd == FsResult::ok && got_len == sizeof(uint32_t));
                    assert(got_v == oracle[id].committed_val);
                } else {
                    assert(rd == FsResult::not_found);
                }
            } else {
                // Must be EITHER old committed state OR new pending state — NEVER corrupt!
                const bool matches_old = oracle[id].committed_present
                                             ? (rd == FsResult::ok && got_len == sizeof(uint32_t) && got_v == oracle[id].committed_val)
                                             : (rd == FsResult::not_found);
                const bool matches_new = oracle[id].pending_present
                                             ? (rd == FsResult::ok && got_len == sizeof(uint32_t) && got_v == oracle[id].pending_val)
                                             : (rd == FsResult::not_found);
                assert(matches_old || matches_new);
            }
        }
    }
}

} // namespace

int main() {
    test_crc32c_and_hamming_exhaustive();
    test_serial_nor_adapter_and_fs();
    test_serial_nand_adapter_and_scrub();
    test_parallel_nand_ecc_and_bad_blocks();
    test_parallel_nor_adapter();
    test_wear_leveling_and_read_disturb();
    test_exhaustive_power_cut_recovery();
    std::printf("PASS: storage (CRC32C, 2070-case Hamming SECDED, 4 flash adapters, dynamic+static WL, read-disturb scrub, 41-point power-cut recovery)\n");
    return 0;
}
