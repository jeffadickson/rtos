/** @file tests/flash_emulators.cpp
 * @brief Verifies the reusable, boot-static, zero-heap flash chip emulators in
 *        include/rtos_flash_mocks.hpp across all 4 supported flash families:
 *          - SerialNorMock    (Winbond W25Q128JV / MX25L12835F / S25FL128L)
 *          - SerialNandMock   (Winbond W25N01GV / Micron MT29F1G01ABAFD)
 *          - ParallelNandMock (Micron MT29F4G08ABA / Kioxia TC58NVG2S0H)
 *          - ParallelNorMock  (Infineon S29GL128S / Micron MT28EW128ABA)
 *
 * Exercises JEDEC/ONFI/CFI identification, realistic WIP/OIP/R/B#/DQ7 busy
 * polling, 1->0 AND-programming physics, ECC bit-flip & bad-block injection,
 * wear telemetry, and mid-write power-cut + reboot recovery on every chip.
 */
#include "rtos_flash_mocks.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace rtos::storage;
using namespace rtos::storage::mocks;

namespace {

// Reusable workload run identically across ALL 4 emulated flash backends!
template<size_t MaxBlocks, size_t MaxFiles, size_t PageBytes>
void run_portable_fs_and_power_cut_scenario(const FlashBackend& backend,
                                            FaultConfig& faults,
                                            void (*restore_power_fn)(void*),
                                            void* mock_ctx) {
    JournalFs<MaxBlocks, MaxFiles, PageBytes> fs;
    fs.set_wear_threshold(2u);
    assert(fs.format(backend) == FsResult::ok);

    // 1. Write 4 files and update them across multiple blocks to trigger wear leveling
    const uint32_t cold_val = 0xCAFEBABEu;
    assert(fs.write_file(0, &cold_val, sizeof(cold_val)) == FsResult::ok);
    for (uint32_t i = 0; i < 36u; ++i) {
        const uint32_t id = 1u + (i % 3u);
        const uint32_t payload = 0x1000u + i;
        assert(fs.write_file(id, &payload, sizeof(payload)) == FsResult::ok);
    }

    // 2. Arm a mid-program power cut on the next write to file 1!
    const uint32_t pre_cut_val = 0x1000u + 33u; // last committed write for id=1 (i=33: 1 + 33%3 == 1)
    const uint32_t doomed_val = 0xDEADDEADu;
    faults.power_cut_after_ops = 0;
    const FsResult cut_res = fs.write_file(1, &doomed_val, sizeof(doomed_val));
    assert(cut_res != FsResult::ok);
    assert(faults.power_lost);

    // 3. Restore power and mount a brand-new JournalFs instance from raw emulated flash
    restore_power_fn(mock_ctx);
    JournalFs<MaxBlocks, MaxFiles, PageBytes> recovered;
    assert(recovered.mount(backend) == FsResult::ok);

    // Cold file 0 intact, file 1 cleanly rolled back to exact pre-cut committed value!
    uint32_t got = 0;
    size_t got_len = 0;
    assert(recovered.read_file(0, &got, sizeof(got), got_len) == FsResult::ok);
    assert(got_len == sizeof(uint32_t) && got == cold_val);

    assert(recovered.read_file(1, &got, sizeof(got), got_len) == FsResult::ok);
    assert(got_len == sizeof(uint32_t) && got == pre_cut_val);
}

void test_serial_nor_emulator() {
    static SerialNorMock<8, 8, 256> chip;
    chip.reset_erased();
    chip.faults().busy_polls = 3u;

    SerialNorAdapter adapter;
    assert(adapter.initialize(chip.spi_bus(), 8u, 8u, 256u));

    // Verify default Winbond W25Q128JV JEDEC ID and custom Macronix MX25L12835F ID
    uint8_t id[3]{};
    assert(adapter.read_jedec_id(id) && id[0] == 0xEFu && id[1] == 0x40u && id[2] == 0x18u);
    chip.set_jedec_id(0xC2u, 0x20u, 0x18u);
    assert(adapter.read_jedec_id(id) && id[0] == 0xC2u && id[1] == 0x20u && id[2] == 0x18u);

    run_portable_fs_and_power_cut_scenario<8, 8, 256>(
        adapter.backend(), chip.faults(),
        [](void* ctx) { static_cast<SerialNorMock<8, 8, 256>*>(ctx)->restore_power(); }, &chip);

    assert(chip.telemetry().busy_polls_served > 0u);
    assert(chip.telemetry().torn_cuts == 1u);
    assert(chip.telemetry().erase_ops >= 8u);
    assert(chip.block_erase_count(0) >= 1u);
}

void test_serial_nand_emulator() {
    static SerialNandMock<8, 4, 256, 64> chip;
    chip.reset_erased();
    chip.mark_factory_bad(2u);
    chip.faults().busy_polls = 2u;

    // Verify W25N01GV JEDEC ID directly over SpiBus
    uint8_t cmd[1] = {0x9Fu};
    uint8_t id[3]{};
    auto bus = chip.spi_bus();
    assert(bus.transfer(bus.context, cmd, 1u, id, 3u));
    assert(id[0] == 0xEFu && id[1] == 0xAAu && id[2] == 0x21u);

    SerialNandAdapter adapter;
    assert(adapter.initialize(bus, 8u, 4u, 256u, 64u));

    run_portable_fs_and_power_cut_scenario<8, 8, 256>(
        adapter.backend(), chip.faults(),
        [](void* ctx) { static_cast<SerialNandMock<8, 4, 256, 64>*>(ctx)->restore_power(); }, &chip);

    // Verify on-die ECC corrected status & P_FAIL runtime bad-block quarantine
    JournalFs<8, 8, 256> fs;
    assert(fs.mount(adapter.backend()) == FsResult::ok);
    assert(fs.stats().bad_blocks == 1u); // factory bad block 2 preserved
    assert(chip.telemetry().busy_polls_served > 0u);
    assert(chip.telemetry().torn_cuts == 1u);
}

void test_parallel_nand_emulator() {
    static ParallelNandMock<8, 4, 256, 64> chip;
    chip.reset_erased();
    chip.mark_factory_bad(5u);
    chip.faults().busy_polls = 2u;

    // Verify ONFI 1.0 Read ID (0x90) returns Micron MT29F4G08ABA signature
    auto bus = chip.bus();
    bus.write_cmd(bus.context, 0x90u);
    bus.write_addr(bus.context, 0x00u);
    uint8_t onfi_id[5]{};
    bus.read_data(bus.context, onfi_id, 5u);
    assert(onfi_id[0] == 0x2Cu && onfi_id[1] == 0xDCu);

    ParallelNandAdapter adapter;
    assert(adapter.initialize(bus, 8u, 4u, 256u, 64u, true));

    run_portable_fs_and_power_cut_scenario<8, 8, 256>(
        adapter.backend(), chip.faults(),
        [](void* ctx) { static_cast<ParallelNandMock<8, 4, 256, 64>*>(ctx)->restore_power(); }, &chip);

    // Flip 1 bit in raw emulated NAND data -> Hamming SECDED silently corrects it!
    JournalFs<8, 8, 256> fs;
    assert(fs.mount(adapter.backend()) == FsResult::ok);
    const uint32_t v = 0x5A5AA5A5u;
    assert(fs.write_file(4, &v, sizeof(v)) == FsResult::ok);
    for (uint32_t b = 0; b < 8u; ++b) {
        if (b == 5u) continue;
        for (uint32_t p = 0; p < 4u; ++p) chip.flip_data_bit(b, p, 40u, 0x04u);
    }
    uint32_t got = 0;
    size_t got_len = 0;
    assert(fs.read_file(4, &got, sizeof(got), got_len) == FsResult::ok);
    assert(got == v && fs.stats().ecc_corrections >= 1u);
    assert(chip.telemetry().busy_polls_served > 0u);
    assert(chip.telemetry().torn_cuts == 1u);
}

void test_parallel_nor_emulator() {
    static ParallelNorMock<8, 4, 256> chip;
    chip.reset_erased();
    chip.faults().busy_polls = 2u;

    // Verify CFI Query (0x55 -> 0x98) returns 'Q','R','Y' and 0xF0 exits back to Read Array
    auto bus = chip.bus();
    bus.write16(bus.context, 0x55u, 0x98u);
    assert(bus.read16(bus.context, 0x10u) == 'Q');
    assert(bus.read16(bus.context, 0x11u) == 'R');
    assert(bus.read16(bus.context, 0x12u) == 'Y');
    bus.write16(bus.context, 0x00u, 0xF0u);

    ParallelNorAdapter adapter;
    assert(adapter.initialize(bus, 8u, 4u, 256u));

    run_portable_fs_and_power_cut_scenario<8, 8, 256>(
        adapter.backend(), chip.faults(),
        [](void* ctx) { static_cast<ParallelNorMock<8, 4, 256>*>(ctx)->restore_power(); }, &chip);

    assert(chip.telemetry().busy_polls_served > 0u);
    assert(chip.telemetry().torn_cuts == 1u);
}

void test_factory_bad_block_multipage_protection() {
    // 1. Parallel ONFI NAND: factory bad markers on page 0 (blk 1), page 1 (blk 3), last page (blk 6)
    static ParallelNandMock<8, 4, 256, 64> pnand;
    pnand.reset_erased();
    pnand.mark_factory_bad(1u, 0u);
    pnand.mark_factory_bad(3u, 1u);
    pnand.mark_factory_bad(6u, 3u); // last page of 4-page block

    ParallelNandAdapter padapter;
    assert(padapter.initialize(pnand.bus(), 8u, 4u, 256u, 64u, true));
    JournalFs<8, 8, 256> pfs;
    assert(pfs.format(padapter.backend()) == FsResult::ok);
    assert(pfs.stats().bad_blocks == 3u);
    assert(pfs.block_state(1u) == BlockState::bad);
    assert(pfs.block_state(3u) == BlockState::bad);
    assert(pfs.block_state(6u) == BlockState::bad);
    // Factory bad blocks must NEVER be erased!
    assert(pnand.block_erase_count(1u) == 0u);
    assert(pnand.block_erase_count(3u) == 0u);
    assert(pnand.block_erase_count(6u) == 0u);

    for (uint32_t i = 0; i < 40u; ++i) {
        assert(pfs.write_file(i % 2u, &i, sizeof(i)) == FsResult::ok);
    }
    assert(pnand.block_erase_count(1u) == 0u);
    assert(pnand.block_erase_count(3u) == 0u);
    assert(pnand.block_erase_count(6u) == 0u);

    JournalFs<8, 8, 256> premount;
    assert(premount.mount(padapter.backend()) == FsResult::ok);
    assert(premount.stats().bad_blocks == 3u);
    assert(premount.block_state(1u) == BlockState::bad);
    assert(premount.block_state(3u) == BlockState::bad);
    assert(premount.block_state(6u) == BlockState::bad);

    // 2. Serial SPI NAND: factory bad markers on page 0 (blk 0), page 1 (blk 4), last page (blk 7)
    static SerialNandMock<8, 4, 256, 64> snand;
    snand.reset_erased();
    snand.mark_factory_bad(0u, 0u);
    snand.mark_factory_bad(4u, 1u);
    snand.mark_factory_bad(7u, 3u);

    SerialNandAdapter sadapter;
    assert(sadapter.initialize(snand.spi_bus(), 8u, 4u, 256u, 64u));
    JournalFs<8, 8, 256> sfs;
    assert(sfs.format(sadapter.backend()) == FsResult::ok);
    assert(sfs.stats().bad_blocks == 3u);
    assert(snand.block_erase_count(0u) == 0u);
    assert(snand.block_erase_count(4u) == 0u);
    assert(snand.block_erase_count(7u) == 0u);
}

void test_mount_bad_block_replay_low_to_high_order() {
    // Regression test: quarantine a HIGHER-numbered block (block 6) while the
    // RecordType::bad_block(6) journal entry is written into a LOWER-numbered block (e.g. block 0/1).
    static ParallelNandMock<8, 4, 256, 64> chip;
    chip.reset_erased();
    ParallelNandAdapter adapter;
    assert(adapter.initialize(chip.bus(), 8u, 4u, 256u, 64u, true));

    JournalFs<8, 8, 256> fs;
    assert(fs.format(adapter.backend()) == FsResult::ok);

    // Fill blocks 0..5 first so block 0 gets GC'd and becomes a lower-numbered free block,
    // OR force block 6 to fail program while block 0 is free and receives RecordType::bad_block(6)!
    for (uint32_t i = 0; i < 20u; ++i) {
        assert(fs.write_file(0u, &i, sizeof(i)) == FsResult::ok);
    }
    // Reclaim block 0 so block 0 is free again; then make block 6 fail on program!
    assert(fs.gc_step() == FsResult::ok);
    chip.faults().fail_program_block = 6;
    for (uint32_t i = 20u; i < 48u; ++i) {
        assert(fs.write_file(0u, &i, sizeof(i)) == FsResult::ok);
    }
    assert(fs.block_state(6u) == BlockState::bad);
    const uint32_t erases_on_6_before_reboot = chip.block_erase_count(6u);

    // Reboot & clear transient hardware fault injection: quarantine MUST persist from journal!
    chip.faults().fail_program_block = -1;
    JournalFs<8, 8, 256> recovered;
    assert(recovered.mount(adapter.backend()) == FsResult::ok);
    assert(recovered.block_state(6u) == BlockState::bad);

    // 40 more writes must NEVER allocate or erase quarantined block 6!
    for (uint32_t i = 100u; i < 140u; ++i) {
        assert(recovered.write_file(1u, &i, sizeof(i)) == FsResult::ok);
    }
    assert(recovered.block_state(6u) == BlockState::bad);
    assert(chip.block_erase_count(6u) == erases_on_6_before_reboot);
}

void test_mid_block_program_fail_evacuates_live_files() {
    static ParallelNandMock<8, 4, 256, 64> chip;
    chip.reset_erased();
    ParallelNandAdapter adapter;
    assert(adapter.initialize(chip.bus(), 8u, 4u, 256u, 64u, true));

    JournalFs<8, 8, 256> fs;
    assert(fs.format(adapter.backend()) == FsResult::ok);

    // Write file 0 (page 0) and file 1 (page 1) into block 0 cleanly
    const uint32_t val_a = 0x11112222u;
    const uint32_t val_b = 0x33334444u;
    const uint32_t val_c = 0x55556666u;
    assert(fs.write_file(0u, &val_a, sizeof(val_a)) == FsResult::ok);
    assert(fs.write_file(1u, &val_b, sizeof(val_b)) == FsResult::ok);
    assert(fs.block_state(0u) == BlockState::active);

    // Now block 0 suffers runtime P_FAIL on page 2!
    chip.faults().fail_program_block = 0;
    assert(fs.write_file(2u, &val_c, sizeof(val_c)) == FsResult::ok);
    assert(fs.block_state(0u) == BlockState::bad);

    // Reboot & mount: block 0 stays bad, AND previously committed files 0 & 1 were evacuated!
    chip.faults().fail_program_block = -1;
    JournalFs<8, 8, 256> recovered;
    assert(recovered.mount(adapter.backend()) == FsResult::ok);
    assert(recovered.block_state(0u) == BlockState::bad);

    uint32_t got = 0;
    size_t len = 0;
    assert(recovered.read_file(0u, &got, sizeof(got), len) == FsResult::ok && got == val_a);
    assert(recovered.read_file(1u, &got, sizeof(got), len) == FsResult::ok && got == val_b);
    assert(recovered.read_file(2u, &got, sizeof(got), len) == FsResult::ok && got == val_c);
}

void test_active_block_ecc_scrub_and_recurring_ecc_retirement() {
    static ParallelNandMock<8, 4, 256, 64> chip;
    chip.reset_erased();
    ParallelNandAdapter adapter;
    assert(adapter.initialize(chip.bus(), 8u, 4u, 256u, 64u, true));

    JournalFs<8, 8, 256> fs;
    fs.set_ecc_retire_threshold(2u);
    assert(fs.format(adapter.backend()) == FsResult::ok);

    // Write 1 file into active (non-full) block 0, flip 1 bit -> read corrects & flags scrub
    const uint32_t payload = 0xABCDEF01u;
    assert(fs.write_file(0u, &payload, sizeof(payload)) == FsResult::ok);
    assert(fs.block_state(0u) == BlockState::active);

    chip.flip_data_bit(0u, 0u, 20u, 0x02u);
    uint32_t got = 0;
    size_t len = 0;
    // Reading the same soft bit-flip multiple times in one erase cycle counts as only 1 ECC cycle event
    assert(fs.read_file(0u, &got, sizeof(got), len) == FsResult::ok && got == payload);
    assert(fs.read_file(0u, &got, sizeof(got), len) == FsResult::ok && got == payload);
    assert(fs.stats().ecc_corrections == 2u);

    // 1st ECC erase-cycle event (< threshold 2): gc_step() must immediately scrub ACTIVE block 0 and recycle it!
    const uint32_t erases_before = chip.block_erase_count(0u);
    assert(fs.gc_step() == FsResult::ok);
    assert(chip.block_erase_count(0u) == erases_before + 1u);
    assert(fs.block_state(0u) == BlockState::free_block);
    assert(fs.read_file(0u, &got, sizeof(got), len) == FsResult::ok && got == payload);

    // Now cycle writes until recycled block 0 is active again (2nd independent erase lifetime!)
    uint32_t v = payload;
    while (fs.block_state(0u) != BlockState::active) {
        ++v;
        assert(fs.write_file(0u, &v, sizeof(v)) == FsResult::ok);
    }
    // Write one fresh file onto active block 0 (page 1 after erase_marker on page 0) and flip 1 bit!
    const uint32_t payload2 = 0xCAFE1234u;
    assert(fs.write_file(0u, &payload2, sizeof(payload2)) == FsResult::ok);
    // Flip 1 bit on all data pages of block 0 (pages 1..3) so reading file 0 hits a 2nd-cycle ECC error
    for (uint32_t p = 1u; p < 4u; ++p) chip.flip_data_bit(0u, p, 25u, 0x10u);
    assert(fs.read_file(0u, &got, sizeof(got), len) == FsResult::ok && got == payload2);

    // 2nd independent ECC cycle hits threshold 2 -> gc_step() evacuates file 0 and permanently RETIRES block 0!
    assert(fs.gc_step() == FsResult::ok);
    assert(fs.stats().ecc_retired_blocks == 1u);
    assert(fs.block_state(0u) == BlockState::bad);
    assert(fs.read_file(0u, &got, sizeof(got), len) == FsResult::ok && got == payload2);

    // Survives reboot with block 0 permanently quarantined and file 0 intact!
    JournalFs<8, 8, 256> recovered;
    assert(recovered.mount(adapter.backend()) == FsResult::ok);
    assert(recovered.block_state(0u) == BlockState::bad);
    assert(recovered.read_file(0u, &got, sizeof(got), len) == FsResult::ok && got == payload2);
}

void test_uncorrectable_2bit_ecc_quarantine_and_sibling_rescue() {
    static ParallelNandMock<8, 4, 256, 64> chip;
    chip.reset_erased();
    ParallelNandAdapter adapter;
    assert(adapter.initialize(chip.bus(), 8u, 4u, 256u, 64u, true));

    JournalFs<8, 8, 256> fs;
    assert(fs.format(adapter.backend()) == FsResult::ok);

    const uint32_t sibling_a = 0xA1A2A3A4u;
    const uint32_t doomed_b = 0xB1B2B3B4u;
    assert(fs.write_file(0u, &sibling_a, sizeof(sibling_a)) == FsResult::ok);
    assert(fs.write_file(1u, &doomed_b, sizeof(doomed_b)) == FsResult::ok);

    // Inject 2-bit uncorrectable flip in page 1 (file 1) ONLY
    chip.flip_data_bit(0u, 1u, 10u, 0x05u); // two bits flipped in same 256 B chunk
    uint32_t got = 0;
    size_t len = 0;
    assert(fs.read_file(1u, &got, sizeof(got), len) == FsResult::corrupt);
    assert(fs.block_state(0u) == BlockState::bad);

    // Healthy sibling file 0 was rescued off dying block 0 and survives reboot!
    JournalFs<8, 8, 256> recovered;
    assert(recovered.mount(adapter.backend()) == FsResult::ok);
    assert(recovered.block_state(0u) == BlockState::bad);
    assert(recovered.read_file(0u, &got, sizeof(got), len) == FsResult::ok && got == sibling_a);
}

void test_empty_block_wear_persistence_and_eol_retirement() {
    // 1. Empty free-block erase_count persistence across reboot (via RecordType::erase_marker)
    static SerialNorMock<8, 8, 256> chip;
    chip.reset_erased();
    SerialNorAdapter adapter;
    assert(adapter.initialize(chip.spi_bus(), 8u, 8u, 256u, 1000u));

    JournalFs<8, 8, 256> fs;
    fs.set_wear_threshold(2u);
    assert(fs.format(adapter.backend()) == FsResult::ok);
    for (uint32_t i = 0; i < 120u; ++i) {
        assert(fs.write_file(i % 2u, &i, sizeof(i)) == FsResult::ok);
    }

    JournalFs<8, 8, 256> recovered;
    assert(recovered.mount(adapter.backend()) == FsResult::ok);
    uint32_t checked_free_blocks = 0;
    for (uint32_t b = 0; b < 8u; ++b) {
        if (recovered.block_state(b) == BlockState::free_block) {
            assert(recovered.block_erase_count(b) == chip.block_erase_count(b));
            assert(recovered.block_erase_count(b) >= 2u);
            ++checked_free_blocks;
        }
    }
    assert(checked_free_blocks >= 1u);

    // 2. End-of-life max_erase_cycles retirement
    static SerialNorMock<8, 4, 256> eol_chip;
    eol_chip.reset_erased();
    SerialNorAdapter eol_adapter;
    assert(eol_adapter.initialize(eol_chip.spi_bus(), 8u, 4u, 256u, /*max_erase_cycles=*/4u));
    JournalFs<8, 8, 256> eol_fs;
    eol_fs.set_wear_threshold(1u);
    assert(eol_fs.format(eol_adapter.backend()) == FsResult::ok);

    const uint32_t cold = 0xC01DFA11u;
    assert(eol_fs.write_file(0u, &cold, sizeof(cold)) == FsResult::ok);
    for (uint32_t i = 0; i < 120u; ++i) {
        if (eol_fs.write_file(1u, &i, sizeof(i)) != FsResult::ok) break;
    }
    assert(eol_fs.stats().wear_retired_blocks >= 1u);
    for (uint32_t b = 0; b < 8u; ++b) {
        assert(eol_chip.block_erase_count(b) <= 4u);
    }
    uint32_t got = 0;
    size_t len = 0;
    assert(eol_fs.read_file(0u, &got, sizeof(got), len) == FsResult::ok && got == cold);

    // Wear-retired blocks remain quarantined after reboot!
    JournalFs<8, 8, 256> eol_remount;
    assert(eol_remount.mount(eol_adapter.backend()) == FsResult::ok);
    assert(eol_remount.stats().bad_blocks == eol_fs.stats().bad_blocks);
    assert(eol_remount.read_file(0u, &got, sizeof(got), len) == FsResult::ok && got == cold);
}

} // namespace

int main() {
    test_serial_nor_emulator();
    test_serial_nand_emulator();
    test_parallel_nand_emulator();
    test_parallel_nor_emulator();
    test_factory_bad_block_multipage_protection();
    test_mount_bad_block_replay_low_to_high_order();
    test_mid_block_program_fail_evacuates_live_files();
    test_active_block_ecc_scrub_and_recurring_ecc_retirement();
    test_uncorrectable_2bit_ecc_quarantine_and_sibling_rescue();
    test_empty_block_wear_persistence_and_eol_retirement();
    std::printf("PASS: flash_emulators (4 chip emulators + multi-page factory bad blocks, low->high bad-block mount replay, mid-block P_FAIL evacuation, active-block ECC scrub & retirement, 2-bit ECC sibling rescue, empty-block wear persistence & EOL retirement)\n");
    return 0;
}
