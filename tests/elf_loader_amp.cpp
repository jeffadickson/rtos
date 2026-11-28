/** @file tests/elf_loader_amp.cpp
 * @brief Verifies Phase 4 Optional Static-Arena ELF32 Loader (RV32IM & ARMv7-M)
 *        and lock-free SPSC shared-memory AMP mailbox channel (RTOS_AMP).
 */
#include "rtos_amp.hpp"
#include "rtos_elf_loader.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace rtos;

namespace {

void push_u16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xffu));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xffu));
}

void push_u32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xffu));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xffu));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xffu));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xffu));
}

uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

struct RelocSpec {
    uint32_t offset{};
    uint32_t sym_idx{};
    uint32_t type{};
    int32_t addend{};
};

/// Build a valid little-endian ELF32 ET_REL object in memory with:
///   0: SHT_NULL
///   1: .text (SHF_ALLOC | SHF_EXECINSTR, PROGBITS)
///   2: .bss  (SHF_ALLOC | SHF_WRITE, NOBITS, 16 bytes)
///   3: .strtab
///   4: .symtab (0=null, 1=module_entry in .text, 2=external_sym UNDEF)
///   5: .rela.text (or .rel.text)
std::vector<uint8_t> build_synthetic_elf32(uint16_t e_machine, bool is_rela,
                                           const std::vector<uint8_t>& text_bytes,
                                           const char* extern_sym_name,
                                           const std::vector<RelocSpec>& relocs) {
    // String table: "\0module_entry\0<extern_sym_name>\0"
    std::vector<uint8_t> strtab;
    strtab.push_back(0);
    const uint32_t entry_name_off = static_cast<uint32_t>(strtab.size());
    for (const char* p = "module_entry"; *p; ++p) strtab.push_back(static_cast<uint8_t>(*p));
    strtab.push_back(0);
    const uint32_t ext_name_off = static_cast<uint32_t>(strtab.size());
    for (const char* p = extern_sym_name; *p; ++p) strtab.push_back(static_cast<uint8_t>(*p));
    strtab.push_back(0);
    while ((strtab.size() & 3u) != 0) strtab.push_back(0);

    // Symtab: 3 entries x 16 bytes
    std::vector<uint8_t> symtab;
    // Sym 0: STN_UNDEF
    for (int i = 0; i < 4; ++i) push_u32(symtab, 0);
    // Sym 1: module_entry (section 1, value 0)
    push_u32(symtab, entry_name_off);
    push_u32(symtab, 0u);
    push_u32(symtab, static_cast<uint32_t>(text_bytes.size()));
    symtab.push_back(0x12u); // STB_GLOBAL | STT_FUNC
    symtab.push_back(0u);
    push_u16(symtab, 1u); // .text
    // Sym 2: external undefined symbol
    push_u32(symtab, ext_name_off);
    push_u32(symtab, 0u);
    push_u32(symtab, 0u);
    symtab.push_back(0x10u); // STB_GLOBAL | STT_NOTYPE
    symtab.push_back(0u);
    push_u16(symtab, 0u); // SHN_UNDEF

    // Relocations section
    std::vector<uint8_t> rel_bytes;
    for (const auto& r : relocs) {
        push_u32(rel_bytes, r.offset);
        push_u32(rel_bytes, (r.sym_idx << 8) | (r.type & 0xffu));
        if (is_rela) push_u32(rel_bytes, static_cast<uint32_t>(r.addend));
    }

    // Layout: [Ehdr 52B] [.text] [.strtab] [.symtab] [.rel(a).text] [6 x Shdr 40B]
    const uint32_t text_off = 52u;
    const uint32_t str_off = text_off + static_cast<uint32_t>(text_bytes.size());
    const uint32_t sym_off = str_off + static_cast<uint32_t>(strtab.size());
    const uint32_t rel_off = sym_off + static_cast<uint32_t>(symtab.size());
    const uint32_t sh_off = rel_off + static_cast<uint32_t>(rel_bytes.size());

    std::vector<uint8_t> elf;
    // ELF32 header (52 bytes)
    elf.push_back(0x7fu); elf.push_back('E'); elf.push_back('L'); elf.push_back('F');
    elf.push_back(1u); // ELFCLASS32
    elf.push_back(1u); // ELFDATA2LSB
    elf.push_back(1u); // EV_CURRENT
    for (int i = 0; i < 9; ++i) elf.push_back(0u);
    push_u16(elf, 1u);        // ET_REL
    push_u16(elf, e_machine); // EM_RISCV / EM_ARM
    push_u32(elf, 1u);        // e_version
    push_u32(elf, 0u);        // e_entry
    push_u32(elf, 0u);        // e_phoff
    push_u32(elf, sh_off);    // e_shoff
    push_u32(elf, 0u);        // e_flags
    push_u16(elf, 52u);       // e_ehsize
    push_u16(elf, 0u);        // e_phentsize
    push_u16(elf, 0u);        // e_phnum
    push_u16(elf, 40u);       // e_shentsize
    push_u16(elf, 6u);        // e_shnum
    push_u16(elf, 3u);        // e_shstrndx
    assert(elf.size() == 52u);

    elf.insert(elf.end(), text_bytes.begin(), text_bytes.end());
    elf.insert(elf.end(), strtab.begin(), strtab.end());
    elf.insert(elf.end(), symtab.begin(), symtab.end());
    elf.insert(elf.end(), rel_bytes.begin(), rel_bytes.end());
    assert(elf.size() == sh_off);

    auto push_shdr = [&](uint32_t type, uint32_t flags, uint32_t off, uint32_t sz,
                         uint32_t link, uint32_t info, uint32_t align, uint32_t entsz) {
        push_u32(elf, 0u); // sh_name
        push_u32(elf, type);
        push_u32(elf, flags);
        push_u32(elf, 0u); // sh_addr
        push_u32(elf, off);
        push_u32(elf, sz);
        push_u32(elf, link);
        push_u32(elf, info);
        push_u32(elf, align);
        push_u32(elf, entsz);
    };
    push_shdr(0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u);                                           // 0: NULL
    push_shdr(1u, 0x6u, text_off, static_cast<uint32_t>(text_bytes.size()), 0u, 0u, 4u, 0u); // 1: .text
    push_shdr(8u, 0x3u, 0u, 16u, 0u, 0u, 4u, 0u);                                        // 2: .bss (NOBITS)
    push_shdr(3u, 0u, str_off, static_cast<uint32_t>(strtab.size()), 0u, 0u, 1u, 0u);    // 3: .strtab
    push_shdr(2u, 0u, sym_off, static_cast<uint32_t>(symtab.size()), 3u, 1u, 4u, 16u);   // 4: .symtab
    push_shdr(is_rela ? 4u : 9u, 0u, rel_off, static_cast<uint32_t>(rel_bytes.size()),
              4u, 1u, 4u, is_rela ? 12u : 8u);                                           // 5: .rel(a).text
    return elf;
}

struct IcacheTracker {
    uint32_t calls{};
    uintptr_t last_addr{};
    size_t last_bytes{};
};

void mock_sync_icache(uintptr_t addr, size_t bytes, void* ctx) {
    auto* t = static_cast<IcacheTracker*>(ctx);
    ++t->calls;
    t->last_addr = addr;
    t->last_bytes = bytes;
}

void test_elf_loader_riscv_and_arm() {
    alignas(16) uint8_t arena[256]{};
    // Target address near arena so PC-relative RV32 CALL & ARM Thumb-2 BL stay in range.
    const uintptr_t allowed_fn_addr = reinterpret_cast<uintptr_t>(arena) + 0x100u;
    const elf::ExportSymbol exports[] = {
        {"nexrt_log", allowed_fn_addr, 1u},
        {"nexrt_old_abi", allowed_fn_addr + 4u, 99u},
    };

    // 1. RV32IM ET_REL with R_RISCV_32 + R_RISCV_CALL + R_RISCV_RELAX.
    std::vector<uint8_t> rv_text(16, 0u);
    // Offset 4: auipc ra, 0 (0x00000097); Offset 8: jalr ra, 0(ra) (0x000080e7)
    rv_text[4] = 0x97u;
    rv_text[8] = 0xe7u; rv_text[9] = 0x80u;
    const auto rv_elf = build_synthetic_elf32(243u, true, rv_text, "nexrt_log", {
        {0u, 2u, 1u, 0},   // R_RISCV_32 -> allowed_fn_addr
        {4u, 2u, 18u, 0},  // R_RISCV_CALL -> auipc+jalr delta to allowed_fn_addr
        {4u, 0u, 51u, 0},  // R_RISCV_RELAX -> no-op
    });

    IcacheTracker icache{};
    elf::LoadConfig cfg{};
    cfg.expected_arch = elf::TargetArch::riscv;
    cfg.exports = exports;
    cfg.export_count = 2u;
    cfg.sync_icache = mock_sync_icache;
    cfg.sync_ctx = &icache;

    elf::Loader loader;
    elf::LoadedModule mod{};

    // NXR-147: missing sync_icache hook fails closed!
    elf::LoadConfig bad_cfg = cfg;
    bad_cfg.sync_icache = nullptr;
    assert(loader.load(rv_elf.data(), rv_elf.size(), arena, sizeof(arena), bad_cfg, mod) ==
           elf::LoadStatus::missing_icache_hook);
    assert(icache.calls == 0u);

    // Valid RV32IM load succeeds, patches .text, zeroes .bss, invokes sync_icache!
    std::memset(arena, 0xCC, sizeof(arena));
    assert(loader.load(rv_elf.data(), rv_elf.size(), arena, sizeof(arena), cfg, mod) ==
           elf::LoadStatus::ok);
    assert(mod.state == elf::SlotState::ready);
    assert(mod.entry_address == reinterpret_cast<uintptr_t>(arena));
    assert(mod.relocations_applied == 3u);
    assert(mod.sections_loaded == 2u);
    assert(icache.calls == 1u && icache.last_bytes == mod.arena_used);
    assert(read_u32(arena) == static_cast<uint32_t>(allowed_fn_addr));
    // Verify .bss (16 bytes after 16-byte .text) was zero-filled!
    for (size_t i = 16; i < 32; ++i) assert(arena[i] == 0u);

    // 2. Reject forbidden `malloc` symbol not in allowlist!
    const auto malloc_elf = build_synthetic_elf32(243u, true, rv_text, "malloc", {
        {0u, 2u, 1u, 0},
    });
    assert(loader.load(malloc_elf.data(), malloc_elf.size(), arena, sizeof(arena), cfg, mod) ==
           elf::LoadStatus::unresolved_symbol);
    assert(mod.state == elf::SlotState::faulted);

    // 3. Reject ABI version mismatch!
    const auto abi_elf = build_synthetic_elf32(243u, true, rv_text, "nexrt_old_abi", {
        {0u, 2u, 1u, 0},
    });
    assert(loader.load(abi_elf.data(), abi_elf.size(), arena, sizeof(arena), cfg, mod) ==
           elf::LoadStatus::abi_mismatch);

    // 4. Reject arena too small!
    assert(loader.load(rv_elf.data(), rv_elf.size(), arena, 12u, cfg, mod) ==
           elf::LoadStatus::arena_too_small);

    // 5. ARMv7-M ET_REL with R_ARM_ABS32 + R_ARM_THM_CALL + R_ARM_MOVW_ABS_NC + R_ARM_MOVT_ABS!
    std::vector<uint8_t> arm_text(16, 0u);
    // Offset 4: Thumb-2 BL template (0xF000 0xF800 -> addend -4 encoded as 0xF7FF 0xFFFE)
    arm_text[4] = 0xffu; arm_text[5] = 0xf7u;
    arm_text[6] = 0xfeu; arm_text[7] = 0xffu;
    // Offset 8: MOVW r0, #0 (0xF240 0x0000); Offset 12: MOVT r0, #0 (0xF2C0 0x0000)
    arm_text[8] = 0x40u; arm_text[9] = 0xf2u;
    arm_text[12] = 0xc0u; arm_text[13] = 0xf2u;
    const auto arm_elf = build_synthetic_elf32(40u, false, arm_text, "nexrt_log", {
        {0u, 2u, 2u, 0},   // R_ARM_ABS32
        {4u, 2u, 10u, 0},  // R_ARM_THM_CALL
        {8u, 2u, 43u, 0},  // R_ARM_MOVW_ABS_NC
        {12u, 2u, 44u, 0}, // R_ARM_MOVT_ABS
    });

    // Wrong expected arch fails closed.
    assert(loader.load(arm_elf.data(), arm_elf.size(), arena, sizeof(arena), cfg, mod) ==
           elf::LoadStatus::unsupported_machine);

    cfg.expected_arch = elf::TargetArch::arm;
    assert(loader.load(arm_elf.data(), arm_elf.size(), arena, sizeof(arena), cfg, mod) ==
           elf::LoadStatus::ok);
    assert(mod.state == elf::SlotState::ready);
    assert(mod.relocations_applied == 4u);
    assert(read_u32(arena) == static_cast<uint32_t>(allowed_fn_addr));
    assert(icache.calls == 2u);

    // 6. Bug #5 regression: ARM SHT_REL MOVW/MOVT with NON-ZERO in-place 16-bit addend (+0x1234 and -8)!
    auto encode_mov_imm16 = [](uint16_t base_hw1, uint16_t base_hw2, uint16_t imm16, uint8_t* dst) {
        const uint16_t imm4 = static_cast<uint16_t>((imm16 >> 12) & 0xfu);
        const uint16_t i_bit = static_cast<uint16_t>((imm16 >> 11) & 1u);
        const uint16_t imm3 = static_cast<uint16_t>((imm16 >> 8) & 0x7u);
        const uint16_t imm8 = static_cast<uint16_t>(imm16 & 0xffu);
        const uint16_t hw1 = static_cast<uint16_t>((base_hw1 & 0xfbf0u) | (i_bit << 10) | imm4);
        const uint16_t hw2 = static_cast<uint16_t>((base_hw2 & 0x8f00u) | (imm3 << 12) | imm8);
        dst[0] = static_cast<uint8_t>(hw1 & 0xffu); dst[1] = static_cast<uint8_t>(hw1 >> 8);
        dst[2] = static_cast<uint8_t>(hw2 & 0xffu); dst[3] = static_cast<uint8_t>(hw2 >> 8);
    };
    auto decode_mov_imm16 = [](const uint8_t* p) -> uint16_t {
        const uint16_t hw1 = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
        const uint16_t hw2 = static_cast<uint16_t>(p[2]) | (static_cast<uint16_t>(p[3]) << 8);
        return static_cast<uint16_t>(((hw1 & 0xfu) << 12) | (((hw1 >> 10) & 1u) << 11) |
                                     (((hw2 >> 12) & 0x7u) << 8) | (hw2 & 0xffu));
    };

    // Place a symbol at 0x1000FFF0 so +0x20 carries into the upper 16 bits (0x10010010)!
    const elf::ExportSymbol addend_exports[] = {
        {"near_sym", allowed_fn_addr, 1u},
        {"carry_sym", 0x1000FFF0u, 1u},
    };
    elf::LoadConfig arm_add_cfg = cfg;
    arm_add_cfg.exports = addend_exports;
    arm_add_cfg.export_count = 2u;

    std::vector<uint8_t> arm_add_text(20, 0u);
    encode_mov_imm16(0xF240u, 0x0000u, 0x0020u, &arm_add_text[0]); // MOVW with in-place addend +0x20
    encode_mov_imm16(0xF2C0u, 0x0000u, 0x0020u, &arm_add_text[4]); // MOVT with in-place addend +0x20
    encode_mov_imm16(0xF240u, 0x0000u, 0xFFF8u, &arm_add_text[8]); // MOVW with signed addend -8
    // Offset 12: R_ARM_REL32 (in-place addend = 0); Offset 16: R_ARM_THM_JUMP24 (in-place -4)
    arm_add_text[16] = 0xffu; arm_add_text[17] = 0xf7u;
    arm_add_text[18] = 0xfeu; arm_add_text[19] = 0xbffu & 0xffu;
    const auto arm_add_elf = build_synthetic_elf32(40u, false, arm_add_text, "carry_sym", {
        {0u, 2u, 43u, 0},  // R_ARM_MOVW_ABS_NC -> low16(0x1000FFF0 + 0x20) = 0x0010
        {4u, 2u, 44u, 0},  // R_ARM_MOVT_ABS    -> hi16(0x1000FFF0 + 0x20)  = 0x1001
        {8u, 2u, 43u, 0},  // R_ARM_MOVW_ABS_NC -> low16(0x1000FFF0 - 8)    = 0xFFE8
        {12u, 1u, 3u, 0},  // R_ARM_REL32 -> module_entry - P = -12
        {16u, 1u, 30u, 0}, // R_ARM_THM_JUMP24 -> module_entry
    });
    assert(loader.load(arm_add_elf.data(), arm_add_elf.size(), arena, sizeof(arena), arm_add_cfg, mod) ==
           elf::LoadStatus::ok);
    assert(decode_mov_imm16(arena + 0) == 0x0010u);
    assert(decode_mov_imm16(arena + 4) == 0x1001u);
    assert(decode_mov_imm16(arena + 8) == 0xFFE8u);
    assert(static_cast<int32_t>(read_u32(arena + 12)) == -12);

    // 7. Bug #5 regression: reject unaligned instruction relocation offsets on both ARM and RV32!
    const auto arm_unaligned = build_synthetic_elf32(40u, false, arm_add_text, "carry_sym", {
        {1u, 2u, 43u, 0}, // Odd offset for Thumb-2 MOVW -> reloc_out_of_range
    });
    assert(loader.load(arm_unaligned.data(), arm_unaligned.size(), arena, sizeof(arena), arm_add_cfg, mod) ==
           elf::LoadStatus::reloc_out_of_range);

    cfg.expected_arch = elf::TargetArch::riscv;
    const auto rv_unaligned = build_synthetic_elf32(243u, true, rv_text, "nexrt_log", {
        {2u, 2u, 26u, 0}, // Non-4-byte-aligned R_RISCV_HI20 -> reloc_out_of_range
    });
    assert(loader.load(rv_unaligned.data(), rv_unaligned.size(), arena, sizeof(arena), cfg, mod) ==
           elf::LoadStatus::reloc_out_of_range);

    // 8. RV32 HI20, LO12_I, LO12_S, JAL + out-of-range JAL + unsupported reloc + entry_not_found + bad headers.
    std::vector<uint8_t> rv_full(20, 0u);
    rv_full[0] = 0x37u; // lui x0, 0
    rv_full[4] = 0x13u; // addi x0, x0, 0
    rv_full[8] = 0x23u; // sw x0, 0(x0)
    rv_full[12] = 0x6fu; // jal x0, 0
    const auto rv_full_elf = build_synthetic_elf32(243u, true, rv_full, "nexrt_log", {
        {0u, 2u, 26u, 0x10}, // R_RISCV_HI20
        {4u, 2u, 27u, 0x10}, // R_RISCV_LO12_I
        {8u, 2u, 28u, 0x10}, // R_RISCV_LO12_S
        {12u, 1u, 17u, 0},   // R_RISCV_JAL -> module_entry (delta = -12)
    });
    assert(loader.load(rv_full_elf.data(), rv_full_elf.size(), arena, sizeof(arena), cfg, mod) ==
           elf::LoadStatus::ok);
    assert(mod.relocations_applied == 4u);

    // Out-of-range R_RISCV_JAL (> +/- 1 MiB) fails closed.
    const auto rv_bad_jal = build_synthetic_elf32(243u, true, rv_full, "nexrt_log", {
        {12u, 1u, 17u, 0x00200000}, // +2 MiB -> out of range
    });
    assert(loader.load(rv_bad_jal.data(), rv_bad_jal.size(), arena, sizeof(arena), cfg, mod) ==
           elf::LoadStatus::reloc_out_of_range);

    // Unsupported relocation type fails closed on RV32 and ARM.
    const auto rv_unsup = build_synthetic_elf32(243u, true, rv_full, "nexrt_log", {
        {0u, 2u, 250u, 0},
    });
    assert(loader.load(rv_unsup.data(), rv_unsup.size(), arena, sizeof(arena), cfg, mod) ==
           elf::LoadStatus::unsupported_reloc);
    const auto arm_unsup = build_synthetic_elf32(40u, true, arm_add_text, "carry_sym", {
        {0u, 2u, 250u, 0},
    });
    assert(loader.load(arm_unsup.data(), arm_unsup.size(), arena, sizeof(arena), arm_add_cfg, mod) ==
           elf::LoadStatus::unsupported_reloc);

    // Missing entry symbol fails closed.
    elf::LoadConfig missing_entry = cfg;
    missing_entry.entry_symbol = "does_not_exist";
    assert(loader.load(rv_full_elf.data(), rv_full_elf.size(), arena, sizeof(arena), missing_entry, mod) ==
           elf::LoadStatus::entry_not_found);

    // Bad header / truncated section table / invalid args.
    assert(loader.load(nullptr, 0, arena, sizeof(arena), cfg, mod) == elf::LoadStatus::invalid_argument);
    auto bad_hdr = rv_full_elf;
    bad_hdr[0] = 0u;
    assert(loader.load(bad_hdr.data(), bad_hdr.size(), arena, sizeof(arena), cfg, mod) ==
           elf::LoadStatus::bad_elf_header);
    auto bad_sh = rv_full_elf;
    bad_sh[48] = 0u; bad_sh[49] = 0u; // e_shnum = 0
    assert(loader.load(bad_sh.data(), bad_sh.size(), arena, sizeof(arena), cfg, mod) ==
           elf::LoadStatus::bad_section_table);

    elf::Loader::unload(mod, arena, sizeof(arena));
    assert(mod.state == elf::SlotState::empty && arena[0] == 0u);
}

struct AmpPacket {
    uint32_t seq{};
    uint32_t payload{};
};

void doorbell_inc(void* ctx) {
    ++(*static_cast<uint32_t*>(ctx));
}

void test_amp_channel() {
    amp::AmpChannel<AmpPacket, 8> ch;
    uint32_t doorbells = 0;

    assert(ch.is_empty() && ch.free_slots() == 8u);
    for (uint32_t i = 0; i < 8; ++i) {
        assert(ch.send({i, i * 10u}, doorbell_inc, &doorbells));
    }
    assert(ch.is_full() && ch.available() == 8u && doorbells == 8u);

    // 9th send fails cleanly and increments dropped count.
    assert(!ch.send({99u, 0u}, doorbell_inc, &doorbells));
    assert(ch.dropped_count() == 1u && doorbells == 8u);

    // Drain and stream 5,000 messages across ring wraparound.
    uint32_t expected_seq = 0;
    AmpPacket pkt{};
    while (ch.receive(pkt)) {
        assert(pkt.seq == expected_seq && pkt.payload == expected_seq * 10u);
        ++expected_seq;
    }
    assert(expected_seq == 8u && ch.is_empty());

    for (uint32_t i = 8; i < 5008; ++i) {
        assert(ch.send({i, i ^ 0x55AAu}));
        assert(ch.receive(pkt));
        assert(pkt.seq == i && pkt.payload == (i ^ 0x55AAu));
    }
    assert(ch.is_empty());
    ch.reset();
    assert(ch.dropped_count() == 0u);
}

} // namespace

int main() {
    test_elf_loader_riscv_and_arm();
    test_amp_channel();
    std::printf("PASS: Phase 4 static-arena ELF32 loader (RV32IM + ARMv7-M) and AMP SPSC channel verified.\n");
    return 0;
}
