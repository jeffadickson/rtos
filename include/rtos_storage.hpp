#pragma once
/** @file rtos_storage.hpp
 * @brief Static, zero-heap, power-fail-safe journaling filesystem (RTOS_STORAGE)
 *        with dynamic + static wear leveling, bad-block quarantine, Hamming SECDED
 *        ECC, and unified adapters for Parallel NAND, Parallel NOR, Serial NOR,
 *        and Serial NAND flash devices.
 *
 * All buffers, block tables, and file indices are statically sized at compile time.
 * See docs/datasheets.rst for supported part numbers, opcodes, and vendor datasheets.
 */
#include <stddef.h>
#include <stdint.h>
#include "rtos_config.hpp"
#include "rtos_watchdog.hpp"

namespace rtos::storage {

enum class FlashKind : uint8_t { parallel_nor, parallel_nand, serial_nor, serial_nand };

enum class IoStatus : uint8_t {
    ok,
    ecc_corrected,
    ecc_uncorrectable,
    bad_block,
    timeout,
    invalid,
    bus_error
};

enum class EccStatus : uint8_t {
    clean,
    corrected_1bit,
    corrected_ecc_bit,
    uncorrectable_2bit
};

enum class FsResult : uint8_t {
    ok,
    invalid,
    not_mounted,
    not_found,
    no_space,
    io_error,
    corrupt,
    too_large
};

enum class BlockState : uint8_t {
    free_block,
    active,
    full,
    bad
};

struct Geometry {
    uint32_t page_bytes{};
    uint32_t spare_bytes{};
    uint32_t pages_per_block{};
    uint32_t block_count{};
    uint32_t max_erase_cycles{};
    FlashKind kind{FlashKind::serial_nor};
    bool use_sw_ecc{};
};

/** @brief Unified boot-bound flash HAL implemented by board/protocol adapters. */
struct FlashBackend {
    void* context{};
    Geometry geometry{};
    IoStatus (*read_page)(void* ctx, uint32_t block, uint32_t page,
                          uint8_t* data, size_t data_len,
                          uint8_t* spare, size_t spare_len){};
    IoStatus (*program_page)(void* ctx, uint32_t block, uint32_t page,
                             const uint8_t* data, size_t data_len,
                             const uint8_t* spare, size_t spare_len){};
    IoStatus (*erase_block)(void* ctx, uint32_t block){};
    bool (*is_factory_bad)(void* ctx, uint32_t block){};
};

// -----------------------------------------------------------------------------
// Compact CRC-32C and 22-bit Complementary Hamming SECDED (256 B -> 3 B ECC)
// -----------------------------------------------------------------------------
uint32_t crc32c(const void* data, size_t bytes);
void hamming256_encode(const uint8_t data[256], uint8_t ecc[3]);
EccStatus hamming256_correct(uint8_t data[256], const uint8_t stored_ecc[3]);

// -----------------------------------------------------------------------------
// Protocol Adapters for Serial NOR, Serial NAND, Parallel NAND, and Parallel NOR
// -----------------------------------------------------------------------------

/// Synchronous SPI transaction hook (assert CS, send tx, read rx, deassert CS).
struct SpiBus {
    void* context{};
    bool (*transfer)(void* ctx, const uint8_t* tx, size_t tx_len,
                     uint8_t* rx, size_t rx_len){};
    uint32_t max_poll_iterations{100000u};
};

/** @brief JEDEC Serial SPI/QSPI NOR adapter (Winbond W25Q128JV, MX25L12835F, S25FL128L). */
class SerialNorAdapter {
    SpiBus bus_{};
    Geometry geom_{};
    uint32_t page_program_chunk_{256u};
    bool wait_ready() const;
    bool write_enable() const;
    static IoStatus read_cb(void*, uint32_t, uint32_t, uint8_t*, size_t, uint8_t*, size_t);
    static IoStatus program_cb(void*, uint32_t, uint32_t, const uint8_t*, size_t, const uint8_t*, size_t);
    static IoStatus erase_cb(void*, uint32_t);
    static bool bad_cb(void*, uint32_t);
public:
    constexpr SerialNorAdapter() = default;
    bool initialize(const SpiBus& bus, uint32_t block_count, uint32_t pages_per_block,
                    uint32_t page_bytes, uint32_t max_erase_cycles = 100000u);
    bool read_jedec_id(uint8_t id[3]) const;
    FlashBackend backend();
};

/** @brief JEDEC Serial SPI NAND adapter (Winbond W25N01GV, Micron MT29F1G01ABAFD). */
class SerialNandAdapter {
    SpiBus bus_{};
    Geometry geom_{};
    IoStatus wait_feature_ready(uint8_t& status_out) const;
    bool write_enable() const;
    static IoStatus read_cb(void*, uint32_t, uint32_t, uint8_t*, size_t, uint8_t*, size_t);
    static IoStatus program_cb(void*, uint32_t, uint32_t, const uint8_t*, size_t, const uint8_t*, size_t);
    static IoStatus erase_cb(void*, uint32_t);
    static bool bad_cb(void*, uint32_t);
public:
    constexpr SerialNandAdapter() = default;
    bool initialize(const SpiBus& bus, uint32_t block_count, uint32_t pages_per_block,
                    uint32_t page_bytes, uint32_t spare_bytes = 64u,
                    uint32_t max_erase_cycles = 100000u);
    FlashBackend backend();
};

/// Synchronous 8-bit ONFI Parallel NAND bus callbacks (CLE/ALE/Data/R/B#).
struct ParallelNandBus {
    void* context{};
    void (*write_cmd)(void* ctx, uint8_t cmd){};
    void (*write_addr)(void* ctx, uint8_t addr){};
    void (*write_data)(void* ctx, const uint8_t* bytes, size_t len){};
    void (*read_data)(void* ctx, uint8_t* bytes, size_t len){};
    bool (*is_ready)(void* ctx){};
    uint32_t max_poll_iterations{100000u};
    void (*hw_ecc_reset)(void* ctx){};
    void (*hw_ecc_read)(void* ctx, uint8_t ecc[3]){};
};

/** @brief ONFI 1.0 x8 Parallel NAND adapter (Micron MT29F4G08ABA, Kioxia TC58NVG2S0H).
 * Automatically computes and verifies 22-bit Hamming SECDED ECC per 256-byte chunk
 * in OOB spare bytes when use_sw_ecc=true.
 */
class ParallelNandAdapter {
    ParallelNandBus bus_{};
    Geometry geom_{};
    bool wait_ready() const;
    uint8_t read_status() const;
    void send_row_col_addr(uint32_t col, uint32_t row) const;
    void send_row_addr(uint32_t row) const;
    static IoStatus read_cb(void*, uint32_t, uint32_t, uint8_t*, size_t, uint8_t*, size_t);
    static IoStatus program_cb(void*, uint32_t, uint32_t, const uint8_t*, size_t, const uint8_t*, size_t);
    static IoStatus erase_cb(void*, uint32_t);
    static bool bad_cb(void*, uint32_t);
public:
    constexpr ParallelNandAdapter() = default;
    bool initialize(const ParallelNandBus& bus, uint32_t block_count, uint32_t pages_per_block,
                    uint32_t page_bytes, uint32_t spare_bytes = 64u, bool use_sw_ecc = true,
                    uint32_t max_erase_cycles = 100000u);
    FlashBackend backend();
};

/// Synchronous 16-bit JEDEC/CFI Parallel NOR bus callbacks (Infineon S29GL128S, MT28EW128ABA).
struct ParallelNorBus {
    void* context{};
    void (*write16)(void* ctx, uint32_t word_offset, uint16_t value){};
    uint16_t (*read16)(void* ctx, uint32_t word_offset){};
    uint32_t max_poll_iterations{200000u};
};

/** @brief 16-bit JEDEC/CFI Parallel NOR adapter (Infineon S29GL128S, Micron MT28EW128ABA). */
class ParallelNorAdapter {
    ParallelNorBus bus_{};
    Geometry geom_{};
    bool poll_dq7(uint32_t word_offset, uint16_t expected) const;
    static IoStatus read_cb(void*, uint32_t, uint32_t, uint8_t*, size_t, uint8_t*, size_t);
    static IoStatus program_cb(void*, uint32_t, uint32_t, const uint8_t*, size_t, const uint8_t*, size_t);
    static IoStatus erase_cb(void*, uint32_t);
    static bool bad_cb(void*, uint32_t);
public:
    constexpr ParallelNorAdapter() = default;
    bool initialize(const ParallelNorBus& bus, uint32_t block_count, uint32_t pages_per_block,
                    uint32_t page_bytes, uint32_t max_erase_cycles = 100000u);
    FlashBackend backend();
};

// -----------------------------------------------------------------------------
// Power-Fail-Safe Append-Only Journaling Filesystem (Zero Heap, Wear Leveled)
// -----------------------------------------------------------------------------

inline constexpr uint32_t kJournalMagic = 0x4E584653u; // "NXFS"
inline constexpr uint16_t kJournalVersion = 1u;

enum class RecordType : uint8_t {
    write_file = 1,
    delete_file = 2,
    bad_block = 3,
    erase_marker = 4
};

struct RecordHeader {
    uint32_t magic{};
    uint16_t version{};
    uint8_t type{};
    uint8_t flags{};
    uint32_t file_id{};
    uint32_t payload_bytes{};
    uint64_t sequence{};
    uint32_t erase_count{};
    uint32_t reserved{};
    uint32_t payload_crc32{};
    uint32_t header_crc32{};
};
static_assert(sizeof(RecordHeader) == 40u, "RecordHeader layout must remain fixed and portable");
static_assert(offsetof(RecordHeader, header_crc32) == 36u, "header_crc32 must be the final 4 bytes");

struct FsStats {
    uint32_t total_blocks{};
    uint32_t free_blocks{};
    uint32_t full_blocks{};
    uint32_t bad_blocks{};
    uint32_t live_files{};
    uint32_t min_erase_count{};
    uint32_t max_erase_count{};
    uint32_t gc_runs{};
    uint32_t static_relocations{};
    uint32_t ecc_corrections{};
    uint32_t ecc_retired_blocks{};
    uint32_t wear_retired_blocks{};
    uint32_t torn_records_ignored{};
    uint64_t last_sequence{};
};

/** @brief Static, zero-heap, power-fail-safe journaling filesystem.
 *
 * Every file mutation appends one CRC-32C-sealed page sequentially to the active
 * block. No in-place overwrites ever occur, making this safe across Parallel NAND,
 * Serial NAND, Parallel NOR, and Serial NOR.
 */
template<size_t MaxBlocks, size_t MaxFiles, size_t PageBytes>
class JournalFs {
    static_assert(MaxBlocks >= 3 && MaxBlocks <= 1024, "Need at least 3 blocks for atomic GC");
    static_assert(MaxFiles >= 1 && MaxFiles <= 256, "MaxFiles must be 1..256");
    static_assert(PageBytes >= 128 && PageBytes <= 4096 && (PageBytes % 64 == 0),
                  "PageBytes must be 128..4096 and a multiple of 64");

    struct BlockMeta {
        uint32_t erase_count{};
        uint32_t read_count{};
        uint16_t live_pages{};
        uint16_t next_page{};
        uint16_t ecc_events{};
        uint16_t bad_log_block{UINT16_MAX};
        BlockState state{BlockState::free_block};
        bool needs_scrub{};
        bool ecc_this_cycle{};
    };

    struct FileSlot {
        bool present{};
        uint32_t block{};
        uint32_t page{};
        uint32_t size{};
        uint64_t sequence{};
    };

    FlashBackend backend_{};
    BlockMeta blocks_[MaxBlocks]{};
    FileSlot files_[MaxFiles]{};
    uint8_t page_buf_[PageBytes]{};
    uint8_t relocate_buf_[PageBytes]{};
    uint8_t spare_buf_[64]{};
    uint32_t active_block_{UINT32_MAX};
    uint32_t block_count_{};
    uint32_t wear_threshold_{8u};
    uint32_t read_disturb_limit_{5000u};
    uint16_t ecc_retire_threshold_{3u};
    uint32_t gc_runs_{};
    uint32_t static_relocations_{};
    uint32_t ecc_corrections_{};
    uint32_t ecc_retired_blocks_{};
    uint32_t wear_retired_blocks_{};
    uint32_t torn_ignored_{};
    uint64_t sequence_{};
    bool mounted_{};
    bool evacuating_{};

    static uint32_t compute_header_crc(const RecordHeader& h) {
        return crc32c(&h, offsetof(RecordHeader, header_crc32));
    }

    static bool valid_header(const RecordHeader& h, size_t max_payload) {
        return h.magic == kJournalMagic &&
               h.version == kJournalVersion &&
               h.payload_bytes <= max_payload &&
               h.header_crc32 == compute_header_crc(h);
    }

    bool find_lowest_wear_free(uint32_t& out_block) const {
        bool found = false;
        uint32_t best = 0;
        for (uint32_t b = 0; b < block_count_; ++b) {
            if (blocks_[b].state != BlockState::free_block) continue;
            if (!found || blocks_[b].erase_count < blocks_[best].erase_count) {
                found = true;
                best = b;
            }
        }
        if (found) out_block = best;
        return found;
    }

    void mark_block_bad(uint32_t b) {
        if (b >= block_count_) return;
        blocks_[b].state = BlockState::bad;
        blocks_[b].live_pages = 0;
        blocks_[b].needs_scrub = false;
        if (active_block_ == b) active_block_ = UINT32_MAX;
    }

    bool ensure_active_block() {
        if (active_block_ < block_count_ &&
            blocks_[active_block_].state == BlockState::active &&
            blocks_[active_block_].next_page < backend_.geometry.pages_per_block) {
            return true;
        }
        if (active_block_ < block_count_ && blocks_[active_block_].state == BlockState::active) {
            blocks_[active_block_].state = BlockState::full;
            active_block_ = UINT32_MAX;
        }
        uint32_t candidate = 0;
        if (!find_lowest_wear_free(candidate)) return false;
        blocks_[candidate].state = BlockState::active;
        blocks_[candidate].live_pages = 0;
        active_block_ = candidate;
        return true;
    }

    void compute_wear_bounds(uint32_t& min_e, uint32_t& max_e) const {
        bool any = false;
        min_e = 0;
        max_e = 0;
        for (uint32_t b = 0; b < block_count_; ++b) {
            if (blocks_[b].state == BlockState::bad) continue;
            if (!any) {
                min_e = max_e = blocks_[b].erase_count;
                any = true;
            } else {
                if (blocks_[b].erase_count < min_e) min_e = blocks_[b].erase_count;
                if (blocks_[b].erase_count > max_e) max_e = blocks_[b].erase_count;
            }
        }
    }

    bool persist_bad_block(uint32_t bad_blk) {
        if (bad_blk >= block_count_) return false;
        uint32_t nb = 0;
        uint32_t np = 0;
        uint64_t ns = 0;
        if (append_record(RecordType::bad_block, bad_blk, nullptr, 0, nb, np, ns, false) != FsResult::ok) {
            return false;
        }
        const uint16_t prev = blocks_[bad_blk].bad_log_block;
        if (prev < block_count_ && blocks_[prev].live_pages > 0) {
            --blocks_[prev].live_pages;
        }
        blocks_[bad_blk].bad_log_block = static_cast<uint16_t>(nb);
        ++blocks_[nb].live_pages;
        return true;
    }

    void evacuate_live_files(uint32_t failing_blk) {
        if (evacuating_) return;
        evacuating_ = true;
        if (count_free_blocks() == 0u) {
            (void)gc_step();
        }
        for (uint32_t id = 0; id < MaxFiles; ++id) {
            if (!files_[id].present || files_[id].block != failing_blk) continue;
            size_t len = 0;
            if (read_file_raw(id, relocate_buf_, sizeof(relocate_buf_), len) != FsResult::ok) {
                continue;
            }
            if (count_free_blocks() == 0u && active_block_ >= block_count_) {
                (void)gc_step();
            }
            uint32_t new_blk = 0;
            uint32_t new_pg = 0;
            uint64_t new_seq = 0;
            if (append_record(RecordType::write_file, id, relocate_buf_, len,
                              new_blk, new_pg, new_seq, false) == FsResult::ok) {
                files_[id].block = new_blk;
                files_[id].page = new_pg;
                files_[id].sequence = new_seq;
                ++blocks_[new_blk].live_pages;
            }
        }
        for (uint32_t b = 0; b < block_count_; ++b) {
            if (blocks_[b].state == BlockState::bad && blocks_[b].bad_log_block == failing_blk) {
                if (count_free_blocks() == 0u && active_block_ >= block_count_) {
                    (void)gc_step();
                }
                (void)persist_bad_block(b);
            }
        }
        evacuating_ = false;
    }

    bool write_erase_marker(uint32_t blk) {
        if (backend_.geometry.pages_per_block < 4u) {
            blocks_[blk].next_page = 0u;
            blocks_[blk].state = BlockState::free_block;
            return true;
        }
        if (!write_page_header(blk, 0u, RecordType::erase_marker, blk)) {
            mark_block_bad(blk);
            (void)persist_bad_block(blk);
            return false;
        }
        blocks_[blk].next_page = 1u;
        blocks_[blk].state = BlockState::free_block;
        return true;
    }

    bool write_page_header(uint32_t blk, uint32_t pg, RecordType type, uint32_t file_id) {
        for (size_t i = 0; i < PageBytes; ++i) page_buf_[i] = 0xFFu;
        for (size_t i = 0; i < sizeof(spare_buf_); ++i) spare_buf_[i] = 0xFFu;

        RecordHeader hdr{};
        hdr.magic = kJournalMagic;
        hdr.version = kJournalVersion;
        hdr.type = static_cast<uint8_t>(type);
        hdr.file_id = file_id;
        hdr.payload_bytes = 0u;
        hdr.sequence = sequence_ + 1u;
        hdr.erase_count = blocks_[blk].erase_count;
        hdr.payload_crc32 = 0u;
        hdr.header_crc32 = compute_header_crc(hdr);

        const auto* hbytes = reinterpret_cast<const uint8_t*>(&hdr);
        for (size_t i = 0; i < sizeof(RecordHeader); ++i) page_buf_[i] = hbytes[i];

        const size_t sp_len = backend_.geometry.spare_bytes < sizeof(spare_buf_)
                                  ? backend_.geometry.spare_bytes
                                  : sizeof(spare_buf_);
        if (backend_.program_page(backend_.context, blk, pg, page_buf_, PageBytes, spare_buf_, sp_len) != IoStatus::ok) {
            return false;
        }
        ++sequence_;
        return true;
    }

    FsResult read_file_raw(uint32_t file_id, void* output, size_t capacity, size_t& out_size) {
        out_size = 0;
        const auto& slot = files_[file_id];
        if (!slot.present) return FsResult::not_found;
        if (capacity < slot.size) return FsResult::too_large;

        const size_t sp_len = backend_.geometry.spare_bytes < sizeof(spare_buf_)
                                  ? backend_.geometry.spare_bytes
                                  : sizeof(spare_buf_);
        const IoStatus io = backend_.read_page(backend_.context, slot.block, slot.page,
                                               page_buf_, PageBytes, spare_buf_, sp_len);
        if (io == IoStatus::ecc_corrected) {
            ++ecc_corrections_;
            if (!blocks_[slot.block].ecc_this_cycle) {
                blocks_[slot.block].ecc_this_cycle = true;
                if (blocks_[slot.block].ecc_events != UINT16_MAX) ++blocks_[slot.block].ecc_events;
            }
            blocks_[slot.block].needs_scrub = true;
        } else if (io != IoStatus::ok) {
            if (io == IoStatus::ecc_uncorrectable || io == IoStatus::bad_block) {
                const uint32_t bad_blk = slot.block;
                mark_block_bad(bad_blk);
                if (!evacuating_) {
                    evacuate_live_files(bad_blk);
                    persist_bad_block(bad_blk);
                }
                return FsResult::corrupt;
            }
            return FsResult::io_error;
        }
        ++blocks_[slot.block].read_count;
        if (blocks_[slot.block].read_count >= read_disturb_limit_) {
            blocks_[slot.block].needs_scrub = true;
        }

        RecordHeader hdr{};
        auto* hbytes = reinterpret_cast<uint8_t*>(&hdr);
        for (size_t i = 0; i < sizeof(RecordHeader); ++i) hbytes[i] = page_buf_[i];
        if (!valid_header(hdr, max_file_bytes()) || hdr.file_id != file_id || hdr.payload_bytes != slot.size) {
            return FsResult::corrupt;
        }
        if (hdr.payload_bytes > 0 &&
            crc32c(page_buf_ + sizeof(RecordHeader), hdr.payload_bytes) != hdr.payload_crc32) {
            return FsResult::corrupt;
        }
        auto* dst = static_cast<uint8_t*>(output);
        for (size_t i = 0; i < hdr.payload_bytes; ++i) dst[i] = page_buf_[sizeof(RecordHeader) + i];
        out_size = hdr.payload_bytes;
        return FsResult::ok;
    }

    FsResult append_record(RecordType type, uint32_t file_id, const void* payload, size_t payload_len,
                           uint32_t& out_block, uint32_t& out_page, uint64_t& out_seq,
                           bool allow_gc = true) {
        const size_t max_payload = PageBytes - sizeof(RecordHeader);
        if (payload_len > max_payload) return FsResult::too_large;
        for (uint32_t attempt = 0; attempt < block_count_; ++attempt) {
            if (!ensure_active_block()) {
                if (!allow_gc || gc_step() != FsResult::ok) return FsResult::no_space;
                if (!ensure_active_block()) return FsResult::no_space;
            }
            const uint32_t blk = active_block_;
            const uint32_t pg = blocks_[blk].next_page;
            for (size_t i = 0; i < PageBytes; ++i) page_buf_[i] = 0xFFu;
            for (size_t i = 0; i < sizeof(spare_buf_); ++i) spare_buf_[i] = 0xFFu;

            RecordHeader hdr{};
            hdr.magic = kJournalMagic;
            hdr.version = kJournalVersion;
            hdr.type = static_cast<uint8_t>(type);
            hdr.file_id = file_id;
            hdr.payload_bytes = static_cast<uint32_t>(payload_len);
            hdr.sequence = sequence_ + 1u;
            hdr.erase_count = blocks_[blk].erase_count;
            hdr.payload_crc32 = payload_len ? crc32c(payload, payload_len) : 0u;
            hdr.header_crc32 = compute_header_crc(hdr);

            const auto* hbytes = reinterpret_cast<const uint8_t*>(&hdr);
            for (size_t i = 0; i < sizeof(RecordHeader); ++i) page_buf_[i] = hbytes[i];
            const auto* pbytes = static_cast<const uint8_t*>(payload);
            for (size_t i = 0; i < payload_len; ++i) page_buf_[sizeof(RecordHeader) + i] = pbytes[i];

            const size_t sp_len = backend_.geometry.spare_bytes < sizeof(spare_buf_)
                                      ? backend_.geometry.spare_bytes
                                      : sizeof(spare_buf_);
            const IoStatus io = backend_.program_page(backend_.context, blk, pg,
                                                      page_buf_, PageBytes, spare_buf_, sp_len);
            if (io == IoStatus::ok) {
                ++sequence_;
                ++blocks_[blk].next_page;
                if (blocks_[blk].next_page >= backend_.geometry.pages_per_block) {
                    blocks_[blk].state = BlockState::full;
                    active_block_ = UINT32_MAX;
                }
                out_block = blk;
                out_page = pg;
                out_seq = sequence_;
                return FsResult::ok;
            }
            // Runtime program failure -> evacuate any already-live files off `blk`,
            // permanently quarantine `blk`, journal bad_block, and retry on a fresh block!
            mark_block_bad(blk);
            if (type != RecordType::bad_block) {
                evacuate_live_files(blk);
                persist_bad_block(blk);
            }
        }
        return FsResult::io_error;
    }

    uint32_t count_free_blocks() const {
        uint32_t c = 0;
        for (uint32_t b = 0; b < block_count_; ++b) {
            if (blocks_[b].state == BlockState::free_block) ++c;
        }
        return c;
    }

public:
    constexpr JournalFs() = default;
    JournalFs(const JournalFs&) = delete;
    JournalFs& operator=(const JournalFs&) = delete;

    static constexpr size_t max_file_bytes() { return PageBytes - sizeof(RecordHeader); }

    void set_wear_threshold(uint32_t spread) { wear_threshold_ = spread > 0 ? spread : 1u; }
    void set_read_disturb_limit(uint32_t reads) { read_disturb_limit_ = reads > 0 ? reads : 1u; }
    void set_ecc_retire_threshold(uint16_t events) { ecc_retire_threshold_ = events > 0 ? events : 1u; }

    BlockState block_state(uint32_t b) const {
        return b < block_count_ ? blocks_[b].state : BlockState::bad;
    }

    uint32_t block_erase_count(uint32_t b) const {
        return b < block_count_ ? blocks_[b].erase_count : 0u;
    }

    /** @brief Format every healthy block (skipping factory bad blocks) and mount cleanly. */
    FsResult format(const FlashBackend& backend) {
        if (!backend.read_page || !backend.program_page || !backend.erase_block ||
            backend.geometry.page_bytes != PageBytes ||
            backend.geometry.block_count < 3u ||
            backend.geometry.block_count > MaxBlocks ||
            backend.geometry.pages_per_block == 0u) {
            return FsResult::invalid;
        }
        backend_ = backend;
        block_count_ = backend.geometry.block_count;
        active_block_ = UINT32_MAX;
        sequence_ = 0;
        gc_runs_ = static_relocations_ = ecc_corrections_ = ecc_retired_blocks_ = wear_retired_blocks_ = torn_ignored_ = 0;
        evacuating_ = false;
        for (size_t i = 0; i < MaxFiles; ++i) files_[i] = {};
        uint32_t healthy = 0;
        for (uint32_t b = 0; b < block_count_; ++b) {
            blocks_[b] = {};
            if (backend_.is_factory_bad && backend_.is_factory_bad(backend_.context, b)) {
                blocks_[b].state = BlockState::bad;
                continue;
            }
            if (backend_.erase_block(backend_.context, b) != IoStatus::ok) {
                blocks_[b].state = BlockState::bad;
                continue;
            }
            blocks_[b].erase_count = 1u;
            blocks_[b].state = BlockState::free_block;
            ++healthy;
        }
        if (healthy < 3u) return FsResult::io_error;
        mounted_ = true;
        return FsResult::ok;
    }

    /** @brief Power-fail-safe deterministic mount: replays all valid CRC-32C records by sequence. */
    FsResult mount(const FlashBackend& backend) {
        if (!backend.read_page || !backend.program_page || !backend.erase_block ||
            backend.geometry.page_bytes != PageBytes ||
            backend.geometry.block_count < 3u ||
            backend.geometry.block_count > MaxBlocks ||
            backend.geometry.pages_per_block == 0u) {
            return FsResult::invalid;
        }
        backend_ = backend;
        block_count_ = backend.geometry.block_count;
        active_block_ = UINT32_MAX;
        sequence_ = 0;
        gc_runs_ = static_relocations_ = ecc_corrections_ = ecc_retired_blocks_ = wear_retired_blocks_ = torn_ignored_ = 0;
        evacuating_ = false;
        for (size_t i = 0; i < MaxFiles; ++i) files_[i] = {};

        bool journaled_bad[MaxBlocks]{};
        uint64_t bad_log_seq[MaxBlocks]{};
        uint16_t bad_log_blk[MaxBlocks]{};
        for (uint32_t b = 0; b < block_count_; ++b) {
            blocks_[b] = {};
            bad_log_blk[b] = UINT16_MAX;
        }

        const size_t sp_len = backend_.geometry.spare_bytes < sizeof(spare_buf_)
                                  ? backend_.geometry.spare_bytes
                                  : sizeof(spare_buf_);

        // Pass 1: discover factory bad blocks, erase counts, and valid committed records.
        uint64_t best_active_seq = 0;
        for (uint32_t b = 0; b < block_count_; ++b) {
            if (backend_.is_factory_bad && backend_.is_factory_bad(backend_.context, b)) {
                blocks_[b].state = BlockState::bad;
                continue;
            }
            uint32_t data_records = 0;
            uint32_t torn_pages = 0;
            uint64_t block_max_seq = 0;
            bool block_corrupt = false;
            for (uint32_t p = 0; p < backend_.geometry.pages_per_block; ++p) {
                const IoStatus io = backend_.read_page(backend_.context, b, p,
                                                       page_buf_, PageBytes, spare_buf_, sp_len);
                if (io == IoStatus::ecc_corrected) {
                    ++ecc_corrections_;
                    if (!blocks_[b].ecc_this_cycle) {
                        blocks_[b].ecc_this_cycle = true;
                        if (blocks_[b].ecc_events != UINT16_MAX) ++blocks_[b].ecc_events;
                    }
                    blocks_[b].needs_scrub = true;
                } else if (io == IoStatus::ecc_uncorrectable || io == IoStatus::bad_block) {
                    block_corrupt = true;
                    break;
                } else if (io != IoStatus::ok) {
                    return FsResult::io_error;
                }

                bool all_ff = true;
                for (size_t i = 0; i < sizeof(RecordHeader); ++i) {
                    if (page_buf_[i] != 0xFFu) {
                        all_ff = false;
                        break;
                    }
                }
                if (all_ff) continue;

                RecordHeader hdr{};
                auto* hbytes = reinterpret_cast<uint8_t*>(&hdr);
                for (size_t i = 0; i < sizeof(RecordHeader); ++i) hbytes[i] = page_buf_[i];
                const bool hdr_ok = valid_header(hdr, max_file_bytes());
                const bool pay_ok = hdr_ok &&
                                    (hdr.payload_bytes == 0 ||
                                     crc32c(page_buf_ + sizeof(RecordHeader), hdr.payload_bytes) == hdr.payload_crc32);
                if (!pay_ok) {
                    // Torn write from sudden power loss: ignore uncommitted page safely!
                    ++torn_ignored_;
                    ++torn_pages;
                    blocks_[b].next_page = static_cast<uint16_t>(p + 1u);
                    continue;
                }

                blocks_[b].next_page = static_cast<uint16_t>(p + 1u);
                if (hdr.erase_count > blocks_[b].erase_count) blocks_[b].erase_count = hdr.erase_count;
                if (hdr.sequence > sequence_) sequence_ = hdr.sequence;
                if (hdr.sequence > block_max_seq) block_max_seq = hdr.sequence;

                if (hdr.type == static_cast<uint8_t>(RecordType::erase_marker)) {
                    continue;
                }

                ++data_records;
                if (hdr.type == static_cast<uint8_t>(RecordType::bad_block) && hdr.file_id < block_count_) {
                    journaled_bad[hdr.file_id] = true;
                    if (hdr.sequence >= bad_log_seq[hdr.file_id]) {
                        bad_log_seq[hdr.file_id] = hdr.sequence;
                        bad_log_blk[hdr.file_id] = static_cast<uint16_t>(b);
                    }
                } else if (hdr.file_id < MaxFiles) {
                    auto& slot = files_[hdr.file_id];
                    if (hdr.sequence > slot.sequence) {
                        slot.sequence = hdr.sequence;
                        if (hdr.type == static_cast<uint8_t>(RecordType::write_file)) {
                            slot.present = true;
                            slot.block = b;
                            slot.page = p;
                            slot.size = hdr.payload_bytes;
                        } else if (hdr.type == static_cast<uint8_t>(RecordType::delete_file)) {
                            slot.present = false;
                            slot.block = 0;
                            slot.page = 0;
                            slot.size = 0;
                        }
                    }
                }
            }

            if (block_corrupt) {
                blocks_[b].state = BlockState::bad;
            } else if (data_records == 0u && torn_pages == 0u &&
                       blocks_[b].next_page < backend_.geometry.pages_per_block) {
                blocks_[b].state = BlockState::free_block;
            } else if (blocks_[b].next_page >= backend_.geometry.pages_per_block) {
                blocks_[b].state = BlockState::full;
            } else {
                blocks_[b].state = BlockState::full; // Seal partial blocks; only newest stays active
                if (block_max_seq >= best_active_seq) {
                    best_active_seq = block_max_seq;
                    active_block_ = b;
                }
            }
        }

        // Pass 2: apply all journaled bad blocks regardless of block-index ordering!
        for (uint32_t b = 0; b < block_count_; ++b) {
            if (journaled_bad[b]) {
                mark_block_bad(b);
                blocks_[b].bad_log_block = bad_log_blk[b];
            }
        }

        if (active_block_ < block_count_ && blocks_[active_block_].state != BlockState::bad) {
            blocks_[active_block_].state = BlockState::active;
        } else {
            active_block_ = UINT32_MAX;
        }

        // Recompute exact live_pages per healthy block from final replayed file table + bad-block records.
        for (size_t id = 0; id < MaxFiles; ++id) {
            if (files_[id].present && files_[id].block < block_count_) {
                if (blocks_[files_[id].block].state == BlockState::bad) {
                    files_[id].present = false;
                    files_[id].size = 0;
                } else {
                    ++blocks_[files_[id].block].live_pages;
                }
            }
        }
        for (uint32_t b = 0; b < block_count_; ++b) {
            const uint16_t holder = blocks_[b].bad_log_block;
            if (blocks_[b].state == BlockState::bad && holder < block_count_ &&
                blocks_[holder].state != BlockState::bad) {
                ++blocks_[holder].live_pages;
            }
        }

        mounted_ = true;
        return FsResult::ok;
    }

    /** @brief Power-fail-atomic file write (append-only; old version survives any mid-write cut). */
    FsResult write_file(uint32_t file_id, const void* data, size_t length) {
        if (!mounted_) return FsResult::not_mounted;
        if (file_id >= MaxFiles || (!data && length)) return FsResult::invalid;
        if (length > max_file_bytes()) return FsResult::too_large;

        // Keep at least 1 free reserve block for atomic GC and emergency P_FAIL / ECC evacuation.
        if (count_free_blocks() == 0u ||
            (count_free_blocks() <= 1u &&
             (active_block_ >= block_count_ ||
              blocks_[active_block_].next_page >= backend_.geometry.pages_per_block))) {
            (void)gc_step();
        }

        uint32_t blk = 0;
        uint32_t pg = 0;
        uint64_t seq = 0;
        const FsResult r = append_record(RecordType::write_file, file_id, data, length, blk, pg, seq);
        if (r != FsResult::ok) return r;

        auto& slot = files_[file_id];
        if (slot.present && slot.block < block_count_ && blocks_[slot.block].live_pages > 0) {
            --blocks_[slot.block].live_pages;
        }
        slot.present = true;
        slot.block = blk;
        slot.page = pg;
        slot.size = static_cast<uint32_t>(length);
        slot.sequence = seq;
        ++blocks_[blk].live_pages;
        return FsResult::ok;
    }

    /** @brief Read latest committed file payload and verify CRC-32C + ECC. */
    FsResult read_file(uint32_t file_id, void* output, size_t capacity, size_t& out_size) {
        out_size = 0;
        if (!mounted_) return FsResult::not_mounted;
        if (file_id >= MaxFiles || (!output && capacity)) return FsResult::invalid;
        return read_file_raw(file_id, output, capacity, out_size);
    }

    /** @brief Append a power-fail-safe deletion tombstone. */
    FsResult delete_file(uint32_t file_id) {
        if (!mounted_) return FsResult::not_mounted;
        if (file_id >= MaxFiles) return FsResult::invalid;
        auto& slot = files_[file_id];
        if (!slot.present) return FsResult::not_found;

        if (count_free_blocks() <= 1u &&
            (active_block_ >= block_count_ ||
             blocks_[active_block_].next_page >= backend_.geometry.pages_per_block)) {
            (void)gc_step();
        }

        uint32_t blk = 0;
        uint32_t pg = 0;
        uint64_t seq = 0;
        const FsResult r = append_record(RecordType::delete_file, file_id, nullptr, 0, blk, pg, seq);
        if (r != FsResult::ok) return r;

        if (slot.block < block_count_ && blocks_[slot.block].live_pages > 0) {
            --blocks_[slot.block].live_pages;
        }
        slot.present = false;
        slot.size = 0;
        slot.sequence = seq;
        return FsResult::ok;
    }

    /** @brief Bounded single-block GC + static wear leveling + ECC/read-disturb scrubbing & retirement.
     * Relocates live files FIRST, commits them, then erases or permanently retires the victim block.
     */
    FsResult gc_step() {
        if (!mounted_) return FsResult::not_mounted;

        uint32_t min_e = 0;
        uint32_t max_e = 0;
        compute_wear_bounds(min_e, max_e);
        const bool static_wl_due = (max_e - min_e) > wear_threshold_;

        auto writable_pages_excluding = [&](uint32_t excluded) -> uint32_t {
            uint32_t avail = 0u;
            for (uint32_t b = 0; b < block_count_; ++b) {
                if (b == excluded) continue;
                if (blocks_[b].state == BlockState::free_block || blocks_[b].state == BlockState::active) {
                    if (blocks_[b].next_page < backend_.geometry.pages_per_block) {
                        avail += backend_.geometry.pages_per_block - blocks_[b].next_page;
                    }
                }
            }
            return avail;
        };

        // Victim selection priority (each candidate must fit its live_pages in remaining writable space):
        //   1. Any full OR active block flagged needs_scrub (ECC 1-bit correction or read disturb)
        //   2. Coldest (lowest erase_count) full block when static wear spread exceeds threshold
        //   3. Full block with fewest live_pages (tie-broken by lowest erase_count)
        uint32_t victim = UINT32_MAX;
        bool is_static_relocation = false;

        for (uint32_t b = 0; b < block_count_; ++b) {
            if ((blocks_[b].state == BlockState::full || blocks_[b].state == BlockState::active) &&
                blocks_[b].needs_scrub &&
                blocks_[b].live_pages <= writable_pages_excluding(b)) {
                victim = b;
                break;
            }
        }
        if (victim == UINT32_MAX && static_wl_due) {
            for (uint32_t b = 0; b < block_count_; ++b) {
                if (blocks_[b].state != BlockState::full) continue;
                if (blocks_[b].live_pages > writable_pages_excluding(b)) continue;
                if (victim == UINT32_MAX || blocks_[b].erase_count < blocks_[victim].erase_count) {
                    victim = b;
                }
            }
            if (victim != UINT32_MAX) is_static_relocation = true;
        }
        if (victim == UINT32_MAX) {
            for (uint32_t b = 0; b < block_count_; ++b) {
                if (blocks_[b].state != BlockState::full) continue;
                if (blocks_[b].live_pages > writable_pages_excluding(b)) continue;
                if (victim == UINT32_MAX ||
                    blocks_[b].live_pages < blocks_[victim].live_pages ||
                    (blocks_[b].live_pages == blocks_[victim].live_pages &&
                     blocks_[b].erase_count < blocks_[victim].erase_count)) {
                    victim = b;
                }
            }
        }
        if (victim == UINT32_MAX) return FsResult::no_space;

        // If scrubbing the currently active block, seal it first so relocations land on a fresh block!
        if (blocks_[victim].state == BlockState::active) {
            blocks_[victim].state = BlockState::full;
            if (active_block_ == victim) active_block_ = UINT32_MAX;
        }

        // Relocate every live file residing in `victim` BEFORE erasing or retiring `victim`.
        for (uint32_t id = 0; id < MaxFiles; ++id) {
            if (!files_[id].present || files_[id].block != victim) continue;
            size_t len = 0;
            const FsResult rd = read_file_raw(id, relocate_buf_, sizeof(relocate_buf_), len);
            if (rd != FsResult::ok) return rd;
            uint32_t new_blk = 0;
            uint32_t new_pg = 0;
            uint64_t new_seq = 0;
            const FsResult wr = append_record(RecordType::write_file, id, relocate_buf_, len,
                                              new_blk, new_pg, new_seq, false);
            if (wr != FsResult::ok) return wr;
            if (blocks_[victim].live_pages > 0) --blocks_[victim].live_pages;
            files_[id].block = new_blk;
            files_[id].page = new_pg;
            files_[id].sequence = new_seq;
            ++blocks_[new_blk].live_pages;
        }

        // Preserve any runtime bad-block quarantine records stored in `victim` BEFORE erasing `victim`!
        for (uint32_t b = 0; b < block_count_; ++b) {
            if (blocks_[b].state == BlockState::bad && blocks_[b].bad_log_block == victim) {
                if (!persist_bad_block(b)) return FsResult::no_space;
            }
        }

        // Retire blocks with recurring ECC degradation permanently after live data is safely evacuated.
        if (blocks_[victim].ecc_events >= ecc_retire_threshold_) {
            mark_block_bad(victim);
            persist_bad_block(victim);
            ++ecc_retired_blocks_;
            ++gc_runs_;
            return FsResult::ok;
        }

        // Erase victim block; on erase failure quarantine permanently!
        if (backend_.erase_block(backend_.context, victim) != IoStatus::ok) {
            mark_block_bad(victim);
            persist_bad_block(victim);
            return FsResult::io_error;
        }
        ++blocks_[victim].erase_count;
        blocks_[victim].read_count = 0;
        blocks_[victim].live_pages = 0;
        blocks_[victim].next_page = 0;
        blocks_[victim].needs_scrub = false;
        blocks_[victim].ecc_this_cycle = false;

        // End-of-life wear retirement once max_erase_cycles is reached.
        if (backend_.geometry.max_erase_cycles > 0u &&
            blocks_[victim].erase_count >= backend_.geometry.max_erase_cycles) {
            mark_block_bad(victim);
            if (!persist_bad_block(victim)) {
                (void)write_page_header(victim, 0u, RecordType::bad_block, victim);
            }
            ++wear_retired_blocks_;
            ++gc_runs_;
            return FsResult::ok;
        }

        if (!write_erase_marker(victim)) {
            return FsResult::io_error;
        }
        ++gc_runs_;
        if (is_static_relocation) ++static_relocations_;
        return FsResult::ok;
    }

    FsStats stats() const {
        FsStats s{};
        s.total_blocks = block_count_;
        s.gc_runs = gc_runs_;
        s.static_relocations = static_relocations_;
        s.ecc_corrections = ecc_corrections_;
        s.ecc_retired_blocks = ecc_retired_blocks_;
        s.wear_retired_blocks = wear_retired_blocks_;
        s.torn_records_ignored = torn_ignored_;
        s.last_sequence = sequence_;
        compute_wear_bounds(s.min_erase_count, s.max_erase_count);
        for (uint32_t b = 0; b < block_count_; ++b) {
            if (blocks_[b].state == BlockState::free_block) ++s.free_blocks;
            else if (blocks_[b].state == BlockState::full) ++s.full_blocks;
            else if (blocks_[b].state == BlockState::bad) ++s.bad_blocks;
        }
        for (size_t id = 0; id < MaxFiles; ++id) {
            if (files_[id].present) ++s.live_files;
        }
        return s;
    }
};

} // namespace rtos::storage
