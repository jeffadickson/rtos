#include "rtos_storage.hpp"

namespace rtos::storage {

namespace {
unsigned popcount32(uint32_t v) {
    unsigned c = 0;
    while (v) {
        v &= (v - 1u);
        ++c;
    }
    return c;
}
} // namespace

uint32_t crc32c(const void* data, size_t bytes) {
    return rtos::watchdog::crc32c(data, bytes);
}

// 22-bit complementary Hamming SECDED over 256 bytes (2048 bits):
//   - p_even[10:0] = XOR of bits where ((bit_index >> k) & 1) == 0
//   - p_odd[10:0]  = XOR of bits where ((bit_index >> k) & 1) == 1
// Any 1-bit data error flips exactly 11 bits ((d_even ^ d_odd) == 0x7FF) at index d_odd.
// Any 1-bit ECC error flips exactly 1 bit. Any 2-bit error fails both checks -> uncorrectable.
void hamming256_encode(const uint8_t data[256], uint8_t ecc[3]) {
    uint32_t p_even = 0;
    uint32_t p_odd = 0;
    for (uint32_t byte_idx = 0; byte_idx < 256u; ++byte_idx) {
        const uint8_t b = data[byte_idx];
        if (b == 0) continue;
        for (uint32_t bit = 0; bit < 8u; ++bit) {
            if (((b >> bit) & 1u) == 0u) continue;
            const uint32_t idx = (byte_idx << 3) | bit;
            p_odd ^= idx;
            p_even ^= (~idx) & 0x7FFu;
        }
    }
    const uint32_t packed = (p_even & 0x7FFu) | ((p_odd & 0x7FFu) << 11);
    ecc[0] = static_cast<uint8_t>(packed & 0xFFu);
    ecc[1] = static_cast<uint8_t>((packed >> 8) & 0xFFu);
    ecc[2] = static_cast<uint8_t>((packed >> 16) & 0x3Fu);
}

EccStatus hamming256_correct(uint8_t data[256], const uint8_t stored_ecc[3]) {
    uint8_t calc[3]{};
    hamming256_encode(data, calc);
    const uint32_t stored = static_cast<uint32_t>(stored_ecc[0]) |
                            (static_cast<uint32_t>(stored_ecc[1]) << 8) |
                            (static_cast<uint32_t>(stored_ecc[2] & 0x3Fu) << 16);
    const uint32_t computed = static_cast<uint32_t>(calc[0]) |
                              (static_cast<uint32_t>(calc[1]) << 8) |
                              (static_cast<uint32_t>(calc[2] & 0x3Fu) << 16);
    const uint32_t diff = stored ^ computed;
    if (diff == 0u) return EccStatus::clean;
    const uint32_t d_even = diff & 0x7FFu;
    const uint32_t d_odd = (diff >> 11) & 0x7FFu;
    if ((d_even ^ d_odd) == 0x7FFu) {
        const uint32_t byte_idx = d_odd >> 3;
        const uint32_t bit = d_odd & 7u;
        data[byte_idx] ^= static_cast<uint8_t>(1u << bit);
        return EccStatus::corrected_1bit;
    }
    if (popcount32(diff) == 1u) {
        return EccStatus::corrected_ecc_bit;
    }
    return EccStatus::uncorrectable_2bit;
}

// =============================================================================
// 1. Serial SPI/QSPI NOR Adapter (Winbond W25Q128JV / MX25L12835F / S25FL128L)
// =============================================================================
bool SerialNorAdapter::initialize(const SpiBus& bus, uint32_t block_count, uint32_t pages_per_block,
                                  uint32_t page_bytes, uint32_t max_erase_cycles) {
    if (!bus.transfer || block_count < 3u || pages_per_block == 0u || page_bytes < 128u) return false;
    bus_ = bus;
    geom_ = {page_bytes, 0u, pages_per_block, block_count, max_erase_cycles, FlashKind::serial_nor, false};
    page_program_chunk_ = page_bytes < 256u ? page_bytes : 256u;
    return true;
}

bool SerialNorAdapter::read_jedec_id(uint8_t id[3]) const {
    if (!bus_.transfer || !id) return false;
    const uint8_t cmd[1] = {0x9Fu};
    return bus_.transfer(bus_.context, cmd, 1u, id, 3u);
}

bool SerialNorAdapter::write_enable() const {
    const uint8_t cmd[1] = {0x06u};
    return bus_.transfer(bus_.context, cmd, 1u, nullptr, 0u);
}

bool SerialNorAdapter::wait_ready() const {
    const uint8_t cmd[1] = {0x05u};
    uint8_t sr = 0xFFu;
    for (uint32_t i = 0; i < bus_.max_poll_iterations; ++i) {
        if (!bus_.transfer(bus_.context, cmd, 1u, &sr, 1u)) return false;
        if ((sr & 0x01u) == 0u) return true;
    }
    return false;
}

IoStatus SerialNorAdapter::read_cb(void* ctx, uint32_t block, uint32_t page,
                                   uint8_t* data, size_t data_len, uint8_t*, size_t) {
    const auto& self = *static_cast<const SerialNorAdapter*>(ctx);
    if (!data || data_len != self.geom_.page_bytes ||
        block >= self.geom_.block_count || page >= self.geom_.pages_per_block) {
        return IoStatus::invalid;
    }
    const uint32_t addr = (block * self.geom_.pages_per_block + page) * self.geom_.page_bytes;
    const uint8_t cmd[4] = {
        0x03u,
        static_cast<uint8_t>((addr >> 16) & 0xFFu),
        static_cast<uint8_t>((addr >> 8) & 0xFFu),
        static_cast<uint8_t>(addr & 0xFFu)
    };
    return self.bus_.transfer(self.bus_.context, cmd, sizeof(cmd), data, data_len)
               ? IoStatus::ok
               : IoStatus::bus_error;
}

IoStatus SerialNorAdapter::program_cb(void* ctx, uint32_t block, uint32_t page,
                                      const uint8_t* data, size_t data_len, const uint8_t*, size_t) {
    const auto& self = *static_cast<const SerialNorAdapter*>(ctx);
    if (!data || data_len != self.geom_.page_bytes ||
        block >= self.geom_.block_count || page >= self.geom_.pages_per_block) {
        return IoStatus::invalid;
    }
    uint32_t base = (block * self.geom_.pages_per_block + page) * self.geom_.page_bytes;
    uint8_t tx[4u + 256u]{};
    for (size_t off = 0; off < data_len; off += self.page_program_chunk_) {
        const size_t chunk = (data_len - off) < self.page_program_chunk_
                                 ? (data_len - off)
                                 : self.page_program_chunk_;
        if (!self.write_enable()) return IoStatus::bus_error;
        const uint32_t addr = base + static_cast<uint32_t>(off);
        tx[0] = 0x02u;
        tx[1] = static_cast<uint8_t>((addr >> 16) & 0xFFu);
        tx[2] = static_cast<uint8_t>((addr >> 8) & 0xFFu);
        tx[3] = static_cast<uint8_t>(addr & 0xFFu);
        for (size_t i = 0; i < chunk; ++i) tx[4u + i] = data[off + i];
        if (!self.bus_.transfer(self.bus_.context, tx, 4u + chunk, nullptr, 0u)) return IoStatus::bus_error;
        if (!self.wait_ready()) return IoStatus::timeout;
    }
    return IoStatus::ok;
}

IoStatus SerialNorAdapter::erase_cb(void* ctx, uint32_t block) {
    const auto& self = *static_cast<const SerialNorAdapter*>(ctx);
    if (block >= self.geom_.block_count) return IoStatus::invalid;
    const uint32_t block_bytes = self.geom_.pages_per_block * self.geom_.page_bytes;
    const uint32_t addr = block * block_bytes;
    if (!self.write_enable()) return IoStatus::bus_error;
    const uint8_t opcode = (block_bytes <= 4096u) ? 0x20u : 0xD8u;
    const uint8_t cmd[4] = {
        opcode,
        static_cast<uint8_t>((addr >> 16) & 0xFFu),
        static_cast<uint8_t>((addr >> 8) & 0xFFu),
        static_cast<uint8_t>(addr & 0xFFu)
    };
    if (!self.bus_.transfer(self.bus_.context, cmd, sizeof(cmd), nullptr, 0u)) return IoStatus::bus_error;
    return self.wait_ready() ? IoStatus::ok : IoStatus::timeout;
}

bool SerialNorAdapter::bad_cb(void*, uint32_t) { return false; }

FlashBackend SerialNorAdapter::backend() {
    return {this, geom_, &read_cb, &program_cb, &erase_cb, &bad_cb};
}

// =============================================================================
// 2. Serial SPI NAND Adapter (Winbond W25N01GV / Micron MT29F1G01ABAFD)
// =============================================================================
bool SerialNandAdapter::initialize(const SpiBus& bus, uint32_t block_count, uint32_t pages_per_block,
                                   uint32_t page_bytes, uint32_t spare_bytes, uint32_t max_erase_cycles) {
    if (!bus.transfer || block_count < 3u || pages_per_block == 0u || page_bytes < 128u || spare_bytes < 4u) {
        return false;
    }
    bus_ = bus;
    geom_ = {page_bytes, spare_bytes, pages_per_block, block_count, max_erase_cycles, FlashKind::serial_nand, false};
    return true;
}

bool SerialNandAdapter::write_enable() const {
    const uint8_t cmd[1] = {0x06u};
    return bus_.transfer(bus_.context, cmd, 1u, nullptr, 0u);
}

IoStatus SerialNandAdapter::wait_feature_ready(uint8_t& status_out) const {
    const uint8_t cmd[2] = {0x0Fu, 0xC0u};
    for (uint32_t i = 0; i < bus_.max_poll_iterations; ++i) {
        uint8_t sr = 0xFFu;
        if (!bus_.transfer(bus_.context, cmd, sizeof(cmd), &sr, 1u)) return IoStatus::bus_error;
        if ((sr & 0x01u) == 0u) {
            status_out = sr;
            return IoStatus::ok;
        }
    }
    return IoStatus::timeout;
}

IoStatus SerialNandAdapter::read_cb(void* ctx, uint32_t block, uint32_t page,
                                    uint8_t* data, size_t data_len, uint8_t* spare, size_t spare_len) {
    const auto& self = *static_cast<const SerialNandAdapter*>(ctx);
    if (!data || data_len != self.geom_.page_bytes ||
        block >= self.geom_.block_count || page >= self.geom_.pages_per_block) {
        return IoStatus::invalid;
    }
    const uint32_t row = block * self.geom_.pages_per_block + page;
    // 0x13: Page Data Read to internal cache
    const uint8_t load_cmd[4] = {
        0x13u,
        static_cast<uint8_t>((row >> 16) & 0xFFu),
        static_cast<uint8_t>((row >> 8) & 0xFFu),
        static_cast<uint8_t>(row & 0xFFu)
    };
    if (!self.bus_.transfer(self.bus_.context, load_cmd, sizeof(load_cmd), nullptr, 0u)) return IoStatus::bus_error;
    uint8_t sr = 0;
    const IoStatus wait = self.wait_feature_ready(sr);
    if (wait != IoStatus::ok) return wait;

    // 0x03: Read data from cache (column 0, 1 dummy byte)
    const uint8_t rd_cmd[4] = {0x03u, 0x00u, 0x00u, 0x00u};
    if (!self.bus_.transfer(self.bus_.context, rd_cmd, sizeof(rd_cmd), data, data_len)) return IoStatus::bus_error;
    if (spare && spare_len > 0) {
        const uint16_t col = static_cast<uint16_t>(self.geom_.page_bytes);
        const uint8_t sp_cmd[4] = {0x03u, static_cast<uint8_t>(col >> 8), static_cast<uint8_t>(col & 0xFFu), 0x00u};
        if (!self.bus_.transfer(self.bus_.context, sp_cmd, sizeof(sp_cmd), spare, spare_len)) {
            return IoStatus::bus_error;
        }
    }
    // W25N01GV / MT29F ECC status bits [5:4]: 00=clean, 01=corrected, 10=uncorrectable
    const uint8_t ecc = static_cast<uint8_t>((sr >> 4) & 0x03u);
    if (ecc == 0x02u) return IoStatus::ecc_uncorrectable;
    if (ecc == 0x01u || ecc == 0x03u) return IoStatus::ecc_corrected;
    return IoStatus::ok;
}

IoStatus SerialNandAdapter::program_cb(void* ctx, uint32_t block, uint32_t page,
                                       const uint8_t* data, size_t data_len,
                                       const uint8_t* spare, size_t spare_len) {
    const auto& self = *static_cast<const SerialNandAdapter*>(ctx);
    if (!data || data_len != self.geom_.page_bytes ||
        block >= self.geom_.block_count || page >= self.geom_.pages_per_block) {
        return IoStatus::invalid;
    }
    if (!self.write_enable()) return IoStatus::bus_error;
    // 0x02: Program Load into cache (256-byte chunks via 0x84 random load after first chunk)
    uint8_t tx[3u + 256u]{};
    for (size_t off = 0; off < data_len; off += 256u) {
        const size_t chunk = (data_len - off) < 256u ? (data_len - off) : 256u;
        tx[0] = (off == 0u) ? 0x02u : 0x84u;
        tx[1] = static_cast<uint8_t>((off >> 8) & 0xFFu);
        tx[2] = static_cast<uint8_t>(off & 0xFFu);
        for (size_t i = 0; i < chunk; ++i) tx[3u + i] = data[off + i];
        if (!self.bus_.transfer(self.bus_.context, tx, 3u + chunk, nullptr, 0u)) return IoStatus::bus_error;
    }
    if (spare && spare_len > 0 && spare_len <= 64u) {
        tx[0] = 0x84u;
        tx[1] = static_cast<uint8_t>((self.geom_.page_bytes >> 8) & 0xFFu);
        tx[2] = static_cast<uint8_t>(self.geom_.page_bytes & 0xFFu);
        for (size_t i = 0; i < spare_len; ++i) tx[3u + i] = spare[i];
        if (!self.bus_.transfer(self.bus_.context, tx, 3u + spare_len, nullptr, 0u)) return IoStatus::bus_error;
    }
    // 0x10: Program Execute to array row
    const uint32_t row = block * self.geom_.pages_per_block + page;
    const uint8_t exec_cmd[4] = {
        0x10u,
        static_cast<uint8_t>((row >> 16) & 0xFFu),
        static_cast<uint8_t>((row >> 8) & 0xFFu),
        static_cast<uint8_t>(row & 0xFFu)
    };
    if (!self.bus_.transfer(self.bus_.context, exec_cmd, sizeof(exec_cmd), nullptr, 0u)) return IoStatus::bus_error;
    uint8_t sr = 0;
    const IoStatus wait = self.wait_feature_ready(sr);
    if (wait != IoStatus::ok) return wait;
    return (sr & 0x08u) ? IoStatus::bad_block : IoStatus::ok; // P_FAIL bit 3
}

IoStatus SerialNandAdapter::erase_cb(void* ctx, uint32_t block) {
    const auto& self = *static_cast<const SerialNandAdapter*>(ctx);
    if (block >= self.geom_.block_count) return IoStatus::invalid;
    if (!self.write_enable()) return IoStatus::bus_error;
    const uint32_t row = block * self.geom_.pages_per_block;
    const uint8_t cmd[4] = {
        0xD8u,
        static_cast<uint8_t>((row >> 16) & 0xFFu),
        static_cast<uint8_t>((row >> 8) & 0xFFu),
        static_cast<uint8_t>(row & 0xFFu)
    };
    if (!self.bus_.transfer(self.bus_.context, cmd, sizeof(cmd), nullptr, 0u)) return IoStatus::bus_error;
    uint8_t sr = 0;
    const IoStatus wait = self.wait_feature_ready(sr);
    if (wait != IoStatus::ok) return wait;
    return (sr & 0x04u) ? IoStatus::bad_block : IoStatus::ok; // E_FAIL bit 2
}

bool SerialNandAdapter::bad_cb(void* ctx, uint32_t block) {
    const auto& self = *static_cast<const SerialNandAdapter*>(ctx);
    if (block >= self.geom_.block_count) return true;
    const uint32_t pages[3] = {
        0u,
        self.geom_.pages_per_block > 1u ? 1u : 0u,
        self.geom_.pages_per_block > 2u ? (self.geom_.pages_per_block - 1u) : 0u
    };
    const uint16_t col = static_cast<uint16_t>(self.geom_.page_bytes);
    const uint8_t rd[4] = {0x03u, static_cast<uint8_t>(col >> 8), static_cast<uint8_t>(col & 0xFFu), 0x00u};
    for (size_t idx = 0; idx < 3u; ++idx) {
        if (idx > 0u && pages[idx] == pages[idx - 1u]) continue;
        const uint32_t row = block * self.geom_.pages_per_block + pages[idx];
        const uint8_t load_cmd[4] = {
            0x13u,
            static_cast<uint8_t>((row >> 16) & 0xFFu),
            static_cast<uint8_t>((row >> 8) & 0xFFu),
            static_cast<uint8_t>(row & 0xFFu)
        };
        if (!self.bus_.transfer(self.bus_.context, load_cmd, sizeof(load_cmd), nullptr, 0u)) return true;
        uint8_t sr = 0;
        if (self.wait_feature_ready(sr) != IoStatus::ok) return true;
        uint8_t marker = 0x00u;
        if (!self.bus_.transfer(self.bus_.context, rd, sizeof(rd), &marker, 1u)) return true;
        if (marker != 0xFFu) return true;
    }
    return false;
}

FlashBackend SerialNandAdapter::backend() {
    return {this, geom_, &read_cb, &program_cb, &erase_cb, &bad_cb};
}

// =============================================================================
// 3. ONFI 1.0 x8 Parallel NAND Adapter (Micron MT29F4G08ABA / Kioxia TC58NVG2S0H)
// =============================================================================
bool ParallelNandAdapter::initialize(const ParallelNandBus& bus, uint32_t block_count, uint32_t pages_per_block,
                                     uint32_t page_bytes, uint32_t spare_bytes, bool use_sw_ecc,
                                     uint32_t max_erase_cycles) {
    if (!bus.write_cmd || !bus.write_addr || !bus.write_data || !bus.read_data || !bus.is_ready ||
        block_count < 3u || pages_per_block == 0u || page_bytes < 256u || (page_bytes % 256u) != 0u) {
        return false;
    }
    const uint32_t ecc_bytes = use_sw_ecc ? (page_bytes / 256u) * 3u : 0u;
    if (spare_bytes < 2u + ecc_bytes || spare_bytes > 64u) return false;
    bus_ = bus;
    geom_ = {page_bytes, spare_bytes, pages_per_block, block_count, max_erase_cycles,
             FlashKind::parallel_nand, use_sw_ecc};
    return true;
}

bool ParallelNandAdapter::wait_ready() const {
    for (uint32_t i = 0; i < bus_.max_poll_iterations; ++i) {
        if (bus_.is_ready(bus_.context)) return true;
    }
    return false;
}

uint8_t ParallelNandAdapter::read_status() const {
    bus_.write_cmd(bus_.context, 0x70u);
    uint8_t st = 0x01u;
    bus_.read_data(bus_.context, &st, 1u);
    return st;
}

void ParallelNandAdapter::send_row_col_addr(uint32_t col, uint32_t row) const {
    bus_.write_addr(bus_.context, static_cast<uint8_t>(col & 0xFFu));
    bus_.write_addr(bus_.context, static_cast<uint8_t>((col >> 8) & 0xFFu));
    bus_.write_addr(bus_.context, static_cast<uint8_t>(row & 0xFFu));
    bus_.write_addr(bus_.context, static_cast<uint8_t>((row >> 8) & 0xFFu));
    bus_.write_addr(bus_.context, static_cast<uint8_t>((row >> 16) & 0xFFu));
}

void ParallelNandAdapter::send_row_addr(uint32_t row) const {
    bus_.write_addr(bus_.context, static_cast<uint8_t>(row & 0xFFu));
    bus_.write_addr(bus_.context, static_cast<uint8_t>((row >> 8) & 0xFFu));
    bus_.write_addr(bus_.context, static_cast<uint8_t>((row >> 16) & 0xFFu));
}

IoStatus ParallelNandAdapter::read_cb(void* ctx, uint32_t block, uint32_t page,
                                      uint8_t* data, size_t data_len, uint8_t* spare, size_t spare_len) {
    const auto& self = *static_cast<const ParallelNandAdapter*>(ctx);
    if (!data || data_len != self.geom_.page_bytes ||
        block >= self.geom_.block_count || page >= self.geom_.pages_per_block) {
        return IoStatus::invalid;
    }
    const uint32_t row = block * self.geom_.pages_per_block + page;
    self.bus_.write_cmd(self.bus_.context, 0x00u);
    self.send_row_col_addr(0u, row);
    self.bus_.write_cmd(self.bus_.context, 0x30u);
    if (!self.wait_ready()) return IoStatus::timeout;

    self.bus_.read_data(self.bus_.context, data, data_len);
    uint8_t oob[64]{};
    self.bus_.read_data(self.bus_.context, oob, self.geom_.spare_bytes);
    if (spare && spare_len > 0) {
        const size_t n = spare_len < self.geom_.spare_bytes ? spare_len : self.geom_.spare_bytes;
        for (size_t i = 0; i < n; ++i) spare[i] = oob[i];
    }

    if (self.geom_.use_sw_ecc) {
        // Erased page (all 0xFF in data) is clean without ECC correction.
        bool all_ff = true;
        for (size_t i = 0; i < data_len && all_ff; ++i) {
            if (data[i] != 0xFFu) all_ff = false;
        }
        if (all_ff) return IoStatus::ok;

        // Power cut mid-page-program before OOB ECC bytes were written leaves all ECC bytes == 0xFF:
        // let JournalFs's CRC-32C cleanly ignore this torn tail page without falsely retiring the block!
        const uint32_t chunks = self.geom_.page_bytes / 256u;
        bool ecc_all_ff = true;
        for (uint32_t i = 0; i < chunks * 3u && ecc_all_ff; ++i) {
            if (oob[2u + i] != 0xFFu) ecc_all_ff = false;
        }
        if (ecc_all_ff) return IoStatus::ok;

        bool corrected = false;
        for (uint32_t c = 0; c < chunks; ++c) {
            const EccStatus es = hamming256_correct(data + c * 256u, oob + 2u + c * 3u);
            if (es == EccStatus::uncorrectable_2bit) return IoStatus::ecc_uncorrectable;
            if (es == EccStatus::corrected_1bit || es == EccStatus::corrected_ecc_bit) corrected = true;
        }
        if (corrected) return IoStatus::ecc_corrected;
    }
    return IoStatus::ok;
}

IoStatus ParallelNandAdapter::program_cb(void* ctx, uint32_t block, uint32_t page,
                                         const uint8_t* data, size_t data_len,
                                         const uint8_t* spare, size_t spare_len) {
    const auto& self = *static_cast<const ParallelNandAdapter*>(ctx);
    if (!data || data_len != self.geom_.page_bytes ||
        block >= self.geom_.block_count || page >= self.geom_.pages_per_block) {
        return IoStatus::invalid;
    }
    uint8_t oob[64]{};
    for (size_t i = 0; i < sizeof(oob); ++i) oob[i] = 0xFFu;
    if (spare && spare_len > 0) {
        const size_t n = spare_len < self.geom_.spare_bytes ? spare_len : self.geom_.spare_bytes;
        for (size_t i = 0; i < n; ++i) oob[i] = spare[i];
    }
    // Keep OOB byte 0..1 == 0xFF (good-block marker) and store Hamming SECDED ECC at OOB[2..].
    oob[0] = 0xFFu;
    oob[1] = 0xFFu;
    if (self.geom_.use_sw_ecc) {
        const uint32_t chunks = self.geom_.page_bytes / 256u;
        for (uint32_t c = 0; c < chunks; ++c) {
            hamming256_encode(data + c * 256u, oob + 2u + c * 3u);
        }
    }

    const uint32_t row = block * self.geom_.pages_per_block + page;
    self.bus_.write_cmd(self.bus_.context, 0x80u);
    self.send_row_col_addr(0u, row);
    self.bus_.write_data(self.bus_.context, data, data_len);
    self.bus_.write_data(self.bus_.context, oob, self.geom_.spare_bytes);
    self.bus_.write_cmd(self.bus_.context, 0x10u);
    if (!self.wait_ready()) return IoStatus::timeout;
    return (self.read_status() & 0x01u) ? IoStatus::bad_block : IoStatus::ok;
}

IoStatus ParallelNandAdapter::erase_cb(void* ctx, uint32_t block) {
    const auto& self = *static_cast<const ParallelNandAdapter*>(ctx);
    if (block >= self.geom_.block_count) return IoStatus::invalid;
    const uint32_t row = block * self.geom_.pages_per_block;
    self.bus_.write_cmd(self.bus_.context, 0x60u);
    self.send_row_addr(row);
    self.bus_.write_cmd(self.bus_.context, 0xD0u);
    if (!self.wait_ready()) return IoStatus::timeout;
    return (self.read_status() & 0x01u) ? IoStatus::bad_block : IoStatus::ok;
}

bool ParallelNandAdapter::bad_cb(void* ctx, uint32_t block) {
    const auto& self = *static_cast<const ParallelNandAdapter*>(ctx);
    if (block >= self.geom_.block_count) return true;
    const uint32_t pages[3] = {
        0u,
        self.geom_.pages_per_block > 1u ? 1u : 0u,
        self.geom_.pages_per_block > 2u ? (self.geom_.pages_per_block - 1u) : 0u
    };
    for (size_t idx = 0; idx < 3u; ++idx) {
        if (idx > 0u && pages[idx] == pages[idx - 1u]) continue;
        const uint32_t row = block * self.geom_.pages_per_block + pages[idx];
        self.bus_.write_cmd(self.bus_.context, 0x00u);
        self.send_row_col_addr(self.geom_.page_bytes, row);
        self.bus_.write_cmd(self.bus_.context, 0x30u);
        if (!self.wait_ready()) return true;
        uint8_t marker = 0x00u;
        self.bus_.read_data(self.bus_.context, &marker, 1u);
        if (marker != 0xFFu) return true;
    }
    return false;
}

FlashBackend ParallelNandAdapter::backend() {
    return {this, geom_, &read_cb, &program_cb, &erase_cb, &bad_cb};
}

// =============================================================================
// 4. 16-Bit JEDEC/CFI Parallel NOR Adapter (Infineon S29GL128S / MT28EW128ABA)
// =============================================================================
bool ParallelNorAdapter::initialize(const ParallelNorBus& bus, uint32_t block_count, uint32_t pages_per_block,
                                    uint32_t page_bytes, uint32_t max_erase_cycles) {
    if (!bus.write16 || !bus.read16 || block_count < 3u || pages_per_block == 0u ||
        page_bytes < 128u || (page_bytes % 2u) != 0u) {
        return false;
    }
    bus_ = bus;
    geom_ = {page_bytes, 0u, pages_per_block, block_count, max_erase_cycles, FlashKind::parallel_nor, false};
    return true;
}

bool ParallelNorAdapter::poll_dq7(uint32_t word_offset, uint16_t expected) const {
    for (uint32_t i = 0; i < bus_.max_poll_iterations; ++i) {
        const uint16_t status = bus_.read16(bus_.context, word_offset);
        if ((status & 0x0080u) == (expected & 0x0080u)) return true;
        if ((status & 0x0020u) != 0u) {
            // DQ5 timeout bit: re-check DQ7 once per JEDEC spec before reporting error.
            const uint16_t again = bus_.read16(bus_.context, word_offset);
            return (again & 0x0080u) == (expected & 0x0080u);
        }
    }
    return false;
}

IoStatus ParallelNorAdapter::read_cb(void* ctx, uint32_t block, uint32_t page,
                                     uint8_t* data, size_t data_len, uint8_t*, size_t) {
    const auto& self = *static_cast<const ParallelNorAdapter*>(ctx);
    if (!data || data_len != self.geom_.page_bytes ||
        block >= self.geom_.block_count || page >= self.geom_.pages_per_block) {
        return IoStatus::invalid;
    }
    const uint32_t word_base = ((block * self.geom_.pages_per_block + page) * self.geom_.page_bytes) / 2u;
    const size_t words = data_len / 2u;
    for (size_t i = 0; i < words; ++i) {
        const uint16_t w = self.bus_.read16(self.bus_.context, word_base + static_cast<uint32_t>(i));
        data[i * 2u] = static_cast<uint8_t>(w & 0xFFu);
        data[i * 2u + 1u] = static_cast<uint8_t>((w >> 8) & 0xFFu);
    }
    return IoStatus::ok;
}

IoStatus ParallelNorAdapter::program_cb(void* ctx, uint32_t block, uint32_t page,
                                        const uint8_t* data, size_t data_len, const uint8_t*, size_t) {
    const auto& self = *static_cast<const ParallelNorAdapter*>(ctx);
    if (!data || data_len != self.geom_.page_bytes ||
        block >= self.geom_.block_count || page >= self.geom_.pages_per_block) {
        return IoStatus::invalid;
    }
    const uint32_t word_base = ((block * self.geom_.pages_per_block + page) * self.geom_.page_bytes) / 2u;
    const size_t words = data_len / 2u;
    for (size_t i = 0; i < words; ++i) {
        const uint16_t w = static_cast<uint16_t>(data[i * 2u]) |
                           (static_cast<uint16_t>(data[i * 2u + 1u]) << 8);
        if (w == 0xFFFFu) continue;
        self.bus_.write16(self.bus_.context, 0x555u, 0x00AAu);
        self.bus_.write16(self.bus_.context, 0x2AAu, 0x0055u);
        self.bus_.write16(self.bus_.context, 0x555u, 0x00A0u);
        const uint32_t waddr = word_base + static_cast<uint32_t>(i);
        self.bus_.write16(self.bus_.context, waddr, w);
        if (!self.poll_dq7(waddr, w)) {
            self.bus_.write16(self.bus_.context, 0u, 0x00F0u); // Reset to read-array mode
            return IoStatus::timeout;
        }
    }
    return IoStatus::ok;
}

IoStatus ParallelNorAdapter::erase_cb(void* ctx, uint32_t block) {
    const auto& self = *static_cast<const ParallelNorAdapter*>(ctx);
    if (block >= self.geom_.block_count) return IoStatus::invalid;
    const uint32_t sector_word = (block * self.geom_.pages_per_block * self.geom_.page_bytes) / 2u;
    self.bus_.write16(self.bus_.context, 0x555u, 0x00AAu);
    self.bus_.write16(self.bus_.context, 0x2AAu, 0x0055u);
    self.bus_.write16(self.bus_.context, 0x555u, 0x0080u);
    self.bus_.write16(self.bus_.context, 0x555u, 0x00AAu);
    self.bus_.write16(self.bus_.context, 0x2AAu, 0x0055u);
    self.bus_.write16(self.bus_.context, sector_word, 0x0030u);
    if (!self.poll_dq7(sector_word, 0xFFFFu)) {
        self.bus_.write16(self.bus_.context, 0u, 0x00F0u);
        return IoStatus::timeout;
    }
    return IoStatus::ok;
}

bool ParallelNorAdapter::bad_cb(void*, uint32_t) { return false; }

FlashBackend ParallelNorAdapter::backend() {
    return {this, geom_, &read_cb, &program_cb, &erase_cb, &bad_cb};
}

} // namespace rtos::storage
