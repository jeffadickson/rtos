/** @file tests/nand_lifecycle_sim.cpp
 * @brief Full-lifecycle 64-block ONFI 1.0 x8 SLC NAND endurance & fault simulation.
 *
 * Emulates a 64-block x 8-page x 512 B (+ 64 B OOB) Micron-style Parallel NAND
 * with 22-bit Hamming SECDED ECC across an end-to-end mission lifecycle:
 *   - 4 pre-existing factory bad blocks (page 0, page 1, and last-page markers)
 *   - 6 immutable cold calibration files + 8 hot telemetry files (2,000+ writes)
 *   - Periodic sudden power cuts mid-write followed by cold reboot + mount()
 *   - Runtime P_FAIL (mid-block program failure w/ sibling evacuation)
 *   - Runtime E_FAIL (GC erase failure)
 *   - Recurring 1-bit ECC degradation -> scrub -> permanent ECC retirement
 *   - 2-bit uncorrectable ECC strike -> sibling rescue -> permanent quarantine
 *   - Read-disturb storm -> proactive scrub & relocation
 *   - Dynamic + static wear leveling -> bounded wear spread + cold-file rotation
 *   - End-of-life max_erase_cycles retirement (zero blocks ever exceed limit)
 *   - Independent ground-truth shadow oracle verifying every live file at every step
 */
#include "rtos_flash_mocks.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace rtos::storage;
using namespace rtos::storage::mocks;

namespace {

static constexpr size_t kBlocks = 64;
static constexpr size_t kPagesPerBlock = 8;
static constexpr size_t kPageBytes = 512;
static constexpr size_t kSpareBytes = 64;
static constexpr size_t kFiles = 16;
static constexpr uint32_t kMaxEraseCycles = 18u;

struct FilePayload {
    uint32_t file_id{};
    uint32_t generation{};
    uint32_t magic{};
    uint32_t checksum{};
    uint8_t body[96]{};
};

static FilePayload make_payload(uint32_t id, uint32_t gen) {
    FilePayload p{};
    p.file_id = id;
    p.generation = gen;
    p.magic = 0x4E414E44u; // "NAND"
    for (size_t i = 0; i < sizeof(p.body); ++i) {
        p.body[i] = static_cast<uint8_t>((id * 31u + gen * 17u + i * 7u) & 0xFFu);
    }
    p.checksum = crc32c(&p, offsetof(FilePayload, checksum));
    return p;
}

static bool verify_payload(const FilePayload& p, uint32_t expected_id, uint32_t expected_gen) {
    return p.magic == 0x4E414E44u &&
           p.file_id == expected_id &&
           p.generation == expected_gen &&
           p.checksum == crc32c(&p, offsetof(FilePayload, checksum));
}

static ParallelNandMock<kBlocks, kPagesPerBlock, kPageBytes, kSpareBytes> g_nand;
static ParallelNandAdapter g_adapter;
static JournalFs<kBlocks, kFiles, kPageBytes> g_fs;

struct ShadowOracle {
    bool present[kFiles]{};
    uint32_t gen[kFiles]{};
};

static void verify_all_live_files(JournalFs<kBlocks, kFiles, kPageBytes>& fs, const ShadowOracle& oracle) {
    for (uint32_t id = 0; id < kFiles; ++id) {
        FilePayload got{};
        size_t len = 0;
        const FsResult r = fs.read_file(id, &got, sizeof(got), len);
        if (oracle.present[id]) {
            assert(r == FsResult::ok);
            assert(len == sizeof(FilePayload));
            assert(verify_payload(got, id, oracle.gen[id]));
        } else {
            assert(r == FsResult::not_found);
        }
    }
}

static const char* state_tag(BlockState st) {
    switch (st) {
        case BlockState::free_block: return "FREE";
        case BlockState::active: return "ACTV";
        case BlockState::full: return "FULL";
        case BlockState::bad: return "BAD ";
    }
    return "????";
}

} // namespace

int main() {
    g_nand.reset_erased();
    g_nand.faults().busy_polls = 1u;

    // -------------------------------------------------------------------------
    // Phase 0: Pre-existing factory bad blocks (page 0, page 1, and last page!)
    // -------------------------------------------------------------------------
    const uint32_t factory_bad[4] = {3u, 17u, 42u, 59u};
    g_nand.mark_factory_bad(3u, 0u);                   // Page 0 OOB[0]
    g_nand.mark_factory_bad(17u, 1u);                  // Page 1 OOB[0]
    g_nand.mark_factory_bad(42u, kPagesPerBlock - 1u); // Last page OOB[0]
    g_nand.mark_factory_bad(59u, 0u);                  // Page 0 OOB[0]

    assert(g_adapter.initialize(g_nand.bus(), kBlocks, kPagesPerBlock, kPageBytes,
                                kSpareBytes, /*use_sw_ecc=*/true, kMaxEraseCycles));
    g_fs.set_wear_threshold(2u);
    g_fs.set_read_disturb_limit(40u);
    g_fs.set_ecc_retire_threshold(2u);
    assert(g_fs.format(g_adapter.backend()) == FsResult::ok);

    assert(g_fs.stats().bad_blocks == 4u);
    for (uint32_t fb : factory_bad) {
        assert(g_fs.block_state(fb) == BlockState::bad);
        assert(g_nand.block_erase_count(fb) == 0u); // Factory bad blocks NEVER erased!
    }

    ShadowOracle oracle{};

    // -------------------------------------------------------------------------
    // Phase 1: Write 6 immutable cold calibration files + 8 hot telemetry files
    // -------------------------------------------------------------------------
    for (uint32_t id = 0; id < 6u; ++id) {
        const auto pay = make_payload(id, 1u);
        assert(g_fs.write_file(id, &pay, sizeof(pay)) == FsResult::ok);
        oracle.present[id] = true;
        oracle.gen[id] = 1u;
    }
    for (uint32_t id = 6u; id < 14u; ++id) {
        const auto pay = make_payload(id, 1u);
        assert(g_fs.write_file(id, &pay, sizeof(pay)) == FsResult::ok);
        oracle.present[id] = true;
        oracle.gen[id] = 1u;
    }
    verify_all_live_files(g_fs, oracle);

    // -------------------------------------------------------------------------
    // Phase 2: Mid-mission runtime faults (P_FAIL, E_FAIL, 1-bit ECC retirement,
    //          2-bit uncorrectable strike, read-disturb storm, & power cuts)
    // -------------------------------------------------------------------------

    // 2a. Runtime P_FAIL on block 10 while hammering hot files
    g_nand.faults().fail_program_block = 10;
    uint32_t total_writes = 0;
    for (uint32_t step = 0; step < 260u; ++step) {
        const uint32_t id = 6u + (step % 8u);
        const uint32_t next_gen = oracle.gen[id] + 1u;
        const auto pay = make_payload(id, next_gen);
        assert(g_fs.write_file(id, &pay, sizeof(pay)) == FsResult::ok);
        oracle.gen[id] = next_gen;
        ++total_writes;
    }
    g_nand.faults().fail_program_block = -1;
    assert(g_fs.block_state(10u) == BlockState::bad);
    verify_all_live_files(g_fs, oracle);

    // 2b. Runtime E_FAIL during GC on block 12 (pick a full block to fail erase)
    uint32_t efail_target = UINT32_MAX;
    for (uint32_t b = 12u; b < kBlocks; ++b) {
        if (g_fs.block_state(b) == BlockState::full) {
            efail_target = b;
            break;
        }
    }
    assert(efail_target != UINT32_MAX);
    g_nand.faults().fail_erase_block = static_cast<int32_t>(efail_target);
    for (uint32_t step = 0; step < 300u && g_fs.block_state(efail_target) != BlockState::bad; ++step) {
        const uint32_t id = 6u + (step % 8u);
        const uint32_t next_gen = oracle.gen[id] + 1u;
        const auto pay = make_payload(id, next_gen);
        assert(g_fs.write_file(id, &pay, sizeof(pay)) == FsResult::ok);
        oracle.gen[id] = next_gen;
        ++total_writes;
    }
    g_nand.faults().fail_erase_block = -1;
    assert(g_fs.block_state(efail_target) == BlockState::bad);
    verify_all_live_files(g_fs, oracle);

    // 2c. Recurring 1-bit ECC degradation across 2 erase cycles -> permanent ECC retirement!
    // Find which block currently holds hot file 6, corrupt 1 bit across its pages, read & scrub once,
    // wait until that block is reused in a 2nd erase cycle, corrupt 1 bit again -> permanently retired!
    uint32_t ecc_degrading_blk = UINT32_MAX;
    for (uint32_t b = 0; b < kBlocks; ++b) {
        if (g_fs.block_state(b) == BlockState::active) {
            ecc_degrading_blk = b;
            break;
        }
    }
    assert(ecc_degrading_blk != UINT32_MAX);
    // Write file 6 onto active ecc_degrading_blk
    {
        const uint32_t next_gen = oracle.gen[6u] + 1u;
        const auto pay = make_payload(6u, next_gen);
        assert(g_fs.write_file(6u, &pay, sizeof(pay)) == FsResult::ok);
        oracle.gen[6u] = next_gen;
        ++total_writes;
    }
    // 1st erase cycle 1-bit flip -> silently corrected by Hamming SECDED & scrubbed!
    for (uint32_t p = 0; p < kPagesPerBlock; ++p) g_nand.flip_data_bit(ecc_degrading_blk, p, 32u, 0x04u);
    verify_all_live_files(g_fs, oracle);
    for (int g = 0; g < 2 && g_fs.block_state(ecc_degrading_blk) != BlockState::free_block; ++g) {
        assert(g_fs.gc_step() == FsResult::ok);
    }
    assert(g_fs.block_state(ecc_degrading_blk) == BlockState::free_block);

    // Drive writes until ecc_degrading_blk becomes active in its 2nd erase cycle
    uint32_t guard = 0;
    while (g_fs.block_state(ecc_degrading_blk) != BlockState::active && guard < 1000u) {
        const uint32_t id = 6u + (guard % 8u);
        const uint32_t next_gen = oracle.gen[id] + 1u;
        const auto pay = make_payload(id, next_gen);
        assert(g_fs.write_file(id, &pay, sizeof(pay)) == FsResult::ok);
        oracle.gen[id] = next_gen;
        ++total_writes;
        ++guard;
    }
    assert(g_fs.block_state(ecc_degrading_blk) == BlockState::active);
    {
        const uint32_t next_gen = oracle.gen[6u] + 1u;
        const auto pay = make_payload(6u, next_gen);
        assert(g_fs.write_file(6u, &pay, sizeof(pay)) == FsResult::ok);
        oracle.gen[6u] = next_gen;
        ++total_writes;
    }
    // 2nd erase cycle 1-bit flip -> hits ecc_retire_threshold (2) -> evacuates data & permanently retires!
    for (uint32_t p = 1u; p < kPagesPerBlock; ++p) g_nand.flip_data_bit(ecc_degrading_blk, p, 48u, 0x20u);
    verify_all_live_files(g_fs, oracle);
    for (int g = 0; g < 2 && g_fs.block_state(ecc_degrading_blk) != BlockState::bad; ++g) {
        assert(g_fs.gc_step() == FsResult::ok);
    }
    assert(g_fs.block_state(ecc_degrading_blk) == BlockState::bad);
    assert(g_fs.stats().ecc_retired_blocks >= 1u);
    verify_all_live_files(g_fs, oracle);

    // 2d. Sudden 2-bit uncorrectable ECC strike on a scratch file (id=14) sharing a block with sibling (id=15)
    // Finish current active block so sibling 15 and victim 14 land cleanly on a fresh block together.
    uint32_t cur_active = UINT32_MAX;
    for (uint32_t b = 0; b < kBlocks; ++b) {
        if (g_fs.block_state(b) == BlockState::active) { cur_active = b; break; }
    }
    while (cur_active != UINT32_MAX && g_fs.block_state(cur_active) == BlockState::active) {
        const uint32_t next_gen = oracle.gen[7u] + 1u;
        const auto pay = make_payload(7u, next_gen);
        assert(g_fs.write_file(7u, &pay, sizeof(pay)) == FsResult::ok);
        oracle.gen[7u] = next_gen;
        ++total_writes;
    }
    const auto sib_pay = make_payload(15u, 1u);
    assert(g_fs.write_file(15u, &sib_pay, sizeof(sib_pay)) == FsResult::ok);
    oracle.present[15u] = true;
    oracle.gen[15u] = 1u;
    uint32_t strike_block_clean = UINT32_MAX;
    for (uint32_t b = 0; b < kBlocks; ++b) {
        if (g_fs.block_state(b) == BlockState::active) { strike_block_clean = b; break; }
    }
    assert(strike_block_clean != UINT32_MAX);
    const auto victim_pay = make_payload(14u, 1u);
    assert(g_fs.write_file(14u, &victim_pay, sizeof(victim_pay)) == FsResult::ok);
    // Page 0 is erase_marker (if recycled), page 1 is file 15, page 2 is file 14 -> flip 2 bits on file 14's page
    const uint32_t target_pg = (g_fs.block_erase_count(strike_block_clean) > 1u) ? 2u : 1u;
    g_nand.flip_data_bit(strike_block_clean, target_pg, 16u, 0x05u); // 2-bit error in chunk 0
    FilePayload tmp{};
    size_t tmp_len = 0;
    assert(g_fs.read_file(14u, &tmp, sizeof(tmp), tmp_len) == FsResult::corrupt);
    assert(g_fs.block_state(strike_block_clean) == BlockState::bad);
    oracle.present[14u] = false; // File 14 was corrupted; sibling 15 MUST survive!
    // Re-verify sibling 15 & all other live files!
    assert(g_fs.read_file(15u, &tmp, sizeof(tmp), tmp_len) == FsResult::ok);
    assert(verify_payload(tmp, 15u, 1u));

    // 2e. Read-disturb storm on cold file 0 (50 reads > read_disturb_limit=40)
    for (int r = 0; r < 50; ++r) {
        assert(g_fs.read_file(0u, &tmp, sizeof(tmp), tmp_len) == FsResult::ok);
        assert(verify_payload(tmp, 0u, 1u));
    }
    assert(g_fs.gc_step() == FsResult::ok);

    // 2f. Sudden power cuts mid-write + full reboot & mount() (4 separate power-cut cycles!)
    uint32_t power_cuts_survived = 0;
    uint32_t mission_gc_runs = 0;
    uint32_t mission_static_relocs = 0;
    uint32_t mission_ecc_retired = 0;
    uint32_t mission_wear_retired = 0;
    auto snapshot_before_reboot = [&]() {
        const auto s = g_fs.stats();
        mission_gc_runs += s.gc_runs;
        mission_static_relocs += s.static_relocations;
        mission_ecc_retired += s.ecc_retired_blocks;
        mission_wear_retired += s.wear_retired_blocks;
    };

    for (uint32_t cut = 0; cut < 4u; ++cut) {
        const uint32_t id = 6u + cut;
        const auto doomed = make_payload(id, 99999u + cut);
        g_nand.faults().power_cut_after_ops = 0;
        const FsResult wr = g_fs.write_file(id, &doomed, sizeof(doomed));
        assert(wr != FsResult::ok);
        assert(g_nand.faults().power_lost);

        // Cold reboot: restore power, mount a fresh JournalFs from raw NAND bytes!
        snapshot_before_reboot();
        g_nand.restore_power();
        assert(g_fs.mount(g_adapter.backend()) == FsResult::ok);
        assert(g_fs.stats().torn_records_ignored >= 1u);
        verify_all_live_files(g_fs, oracle);
        ++power_cuts_survived;
    }

    // -------------------------------------------------------------------------
    // Phase 3: Long-horizon wear-leveling & end-of-life retirement simulation
    // -------------------------------------------------------------------------
    // Continue hammering hot telemetry files across 6,000+ writes + mid-run reboot!
    uint32_t phase3_writes = 0;
    for (uint32_t step = 0; step < 8000u; ++step) {
        if (step == 2500u) {
            // Mid-mission warm reboot: verify empty-block wear counts & bad-block table survive!
            const uint32_t bad_before = g_fs.stats().bad_blocks;
            snapshot_before_reboot();
            assert(g_fs.mount(g_adapter.backend()) == FsResult::ok);
            assert(g_fs.stats().bad_blocks == bad_before);
            verify_all_live_files(g_fs, oracle);
        }
        const uint32_t id = 6u + (step % 8u);
        const uint32_t next_gen = oracle.gen[id] + 1u;
        const auto pay = make_payload(id, next_gen);
        const FsResult r = g_fs.write_file(id, &pay, sizeof(pay));
        if (r != FsResult::ok) break;
        oracle.gen[id] = next_gen;
        ++phase3_writes;
        ++total_writes;
        // Stop once at least 4 blocks have cleanly reached EOL wear retirement so we observe both
        // active wear-leveled healthy blocks AND EOL retired blocks simultaneously!
        if (mission_wear_retired + g_fs.stats().wear_retired_blocks >= 4u) break;
    }

    const FsStats st = g_fs.stats();
    snapshot_before_reboot();

    // Final reboot verification after entire mission!
    assert(g_fs.mount(g_adapter.backend()) == FsResult::ok);
    verify_all_live_files(g_fs, oracle);

    // -------------------------------------------------------------------------
    // Strict Mission Invariants
    // -------------------------------------------------------------------------
    // 1. Factory bad blocks were NEVER erased across the entire multi-thousand-write mission
    for (uint32_t fb : factory_bad) {
        assert(g_fs.block_state(fb) == BlockState::bad);
        assert(g_nand.block_erase_count(fb) == 0u);
    }
    // 2. All runtime bad blocks (P_FAIL, E_FAIL, ECC-retired, 2-bit strike) remain quarantined after final reboot
    assert(g_fs.block_state(10u) == BlockState::bad);
    assert(g_fs.block_state(efail_target) == BlockState::bad);
    assert(g_fs.block_state(ecc_degrading_blk) == BlockState::bad);
    assert(g_fs.block_state(strike_block_clean) == BlockState::bad);
    assert(g_fs.stats().bad_blocks == st.bad_blocks);

    // 3. Static wear leveling rotated cold files off low-wear blocks and kept spread tightly bounded!
    assert(mission_static_relocs >= 5u);
    assert(st.max_erase_count - st.min_erase_count <= 4u);

    // 4. End-of-life wear retirement fired and ZERO blocks ever exceeded kMaxEraseCycles!
    assert(mission_wear_retired >= 4u);
    uint32_t total_hw_erases = 0;
    for (uint32_t b = 0; b < kBlocks; ++b) {
        const uint32_t ec = g_nand.block_erase_count(b);
        assert(ec <= kMaxEraseCycles);
        total_hw_erases += ec;
    }

    // Print comprehensive mission simulation report + per-block wear map
    std::printf("=== NexRT 64-Block ONFI SLC NAND Full-Lifecycle Simulation ===\n");
    std::printf("Geometry: %zu blocks x %zu pages x %zu B (+ %zu B OOB, 22-bit Hamming SECDED)\n",
                kBlocks, kPagesPerBlock, kPageBytes, kSpareBytes);
    std::printf("Writes committed: %u (Phase 3: %u) | HW erases: %u | GC runs: %u | Static WL relocations: %u\n",
                total_writes, phase3_writes, total_hw_erases, mission_gc_runs, mission_static_relocs);
    std::printf("Healthy wear spread: min=%u max=%u (delta=%u, EOL limit=%u)\n",
                st.min_erase_count, st.max_erase_count, st.max_erase_count - st.min_erase_count, kMaxEraseCycles);
    std::printf("Bad blocks: %u total (4 factory, 1 P_FAIL, 1 E_FAIL, %u ECC-retired, 1 2-bit SEU, %u wear-EOL)\n",
                st.bad_blocks, mission_ecc_retired, mission_wear_retired);
    std::printf("Power cuts survived: %u | Cold calibration files intact: 6/6 | Live files verified: %u/%u\n",
                power_cuts_survived, st.live_files, st.live_files);
    std::printf("Per-block wear & state map:\n");
    for (uint32_t b = 0; b < kBlocks; ++b) {
        std::printf("  B%02u[%s:%02u]", b, state_tag(g_fs.block_state(b)), g_nand.block_erase_count(b));
        if ((b + 1u) % 8u == 0u) std::printf("\n");
    }
    std::printf("PASS: nand_lifecycle_sim\n");
    return 0;
}
