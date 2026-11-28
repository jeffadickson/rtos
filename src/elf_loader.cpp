#include "rtos_elf_loader.hpp"

namespace rtos::elf {
namespace {

// Minimal ELF32 constants (no host <elf.h> dependency so firmware stays freestanding).
constexpr uint8_t elfclass32 = 1u;
constexpr uint8_t elfdata2lsb = 1u;
constexpr uint16_t et_rel = 1u;
constexpr uint16_t em_arm = 40u;
constexpr uint16_t em_riscv = 243u;

constexpr uint32_t sht_null = 0u;
constexpr uint32_t sht_progbits = 1u;
constexpr uint32_t sht_symtab = 2u;
constexpr uint32_t sht_strtab = 3u;
constexpr uint32_t sht_rela = 4u;
constexpr uint32_t sht_nobits = 8u;
constexpr uint32_t sht_rel = 9u;

constexpr uint32_t shf_alloc = 0x2u;
constexpr uint16_t shn_undef = 0u;
constexpr uint16_t shn_abs = 0xfff1u;

// RV32IM relocation types
constexpr uint32_t r_riscv_32 = 1u;
constexpr uint32_t r_riscv_jal = 17u;
constexpr uint32_t r_riscv_call = 18u;
constexpr uint32_t r_riscv_call_plt = 19u;
constexpr uint32_t r_riscv_hi20 = 26u;
constexpr uint32_t r_riscv_lo12_i = 27u;
constexpr uint32_t r_riscv_lo12_s = 28u;
constexpr uint32_t r_riscv_relax = 51u;

// ARMv7-M relocation types
constexpr uint32_t r_arm_abs32 = 2u;
constexpr uint32_t r_arm_rel32 = 3u;
constexpr uint32_t r_arm_thm_call = 10u;
constexpr uint32_t r_arm_thm_jump24 = 30u;
constexpr uint32_t r_arm_movw_abs_nc = 43u;
constexpr uint32_t r_arm_movt_abs = 44u;

uint16_t rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void wr16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xffu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xffu);
}

void wr32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xffu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xffu);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xffu);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xffu);
}

bool cstr_eq(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

struct Shdr {
    uint32_t name{};
    uint32_t type{};
    uint32_t flags{};
    uint32_t addr{};
    uint32_t offset{};
    uint32_t size{};
    uint32_t link{};
    uint32_t info{};
    uint32_t addralign{};
    uint32_t entsize{};
};

Shdr read_shdr(const uint8_t* p) {
    Shdr s{};
    s.name = rd32(p + 0);
    s.type = rd32(p + 4);
    s.flags = rd32(p + 8);
    s.addr = rd32(p + 12);
    s.offset = rd32(p + 16);
    s.size = rd32(p + 20);
    s.link = rd32(p + 24);
    s.info = rd32(p + 28);
    s.addralign = rd32(p + 32);
    s.entsize = rd32(p + 36);
    return s;
}

struct Sym {
    uint32_t name{};
    uint32_t value{};
    uint32_t size{};
    uint8_t info{};
    uint8_t other{};
    uint16_t shndx{};
};

Sym read_sym(const uint8_t* p) {
    Sym s{};
    s.name = rd32(p + 0);
    s.value = rd32(p + 4);
    s.size = rd32(p + 8);
    s.info = p[12];
    s.other = p[13];
    s.shndx = rd16(p + 14);
    return s;
}

const char* safe_str(const uint8_t* elf, size_t elf_size, const Shdr& strtab, uint32_t off) {
    if (strtab.type != sht_strtab || off >= strtab.size) return nullptr;
    if (static_cast<size_t>(strtab.offset) + strtab.size > elf_size) return nullptr;
    const uint8_t* base = elf + strtab.offset;
    for (uint32_t i = off; i < strtab.size; ++i) {
        if (base[i] == '\0') return reinterpret_cast<const char*>(base + off);
    }
    return nullptr;
}

LoadStatus resolve_symbol(const Sym& sym, const char* sym_name,
                          const uintptr_t* sec_addrs, const bool* sec_loaded, uint16_t shnum,
                          const LoadConfig& config, uintptr_t& out_addr) {
    if (sym.shndx == shn_abs) {
        out_addr = sym.value;
        return LoadStatus::ok;
    }
    if (sym.shndx == shn_undef) {
        if (!sym_name || sym_name[0] == '\0') return LoadStatus::unresolved_symbol;
        for (size_t i = 0; i < config.export_count; ++i) {
            if (config.exports && cstr_eq(config.exports[i].name, sym_name)) {
                if (config.exports[i].abi_version != config.required_abi_version) {
                    return LoadStatus::abi_mismatch;
                }
                out_addr = config.exports[i].address;
                return LoadStatus::ok;
            }
        }
        return LoadStatus::unresolved_symbol;
    }
    if (sym.shndx >= shnum || !sec_loaded[sym.shndx]) {
        return LoadStatus::unresolved_symbol;
    }
    out_addr = sec_addrs[sym.shndx] + sym.value;
    return LoadStatus::ok;
}

LoadStatus apply_riscv_reloc(uint8_t* target_base, size_t target_size,
                             uint32_t offset, uint32_t type,
                             uintptr_t sym_addr, int32_t addend) {
    if (type == r_riscv_relax) return LoadStatus::ok;
    if (offset + 4u > target_size) return LoadStatus::reloc_out_of_range;

    uint8_t* loc = target_base + offset;
    const uintptr_t P = reinterpret_cast<uintptr_t>(loc);
    const int64_t S = static_cast<int64_t>(sym_addr);
    const int64_t A = static_cast<int64_t>(addend);

    switch (type) {
        case r_riscv_32: {
            wr32(loc, static_cast<uint32_t>(S + A));
            return LoadStatus::ok;
        }
        case r_riscv_hi20: {
            if ((offset & 3u) != 0u) return LoadStatus::reloc_out_of_range;
            const int64_t val = S + A;
            const uint32_t hi = static_cast<uint32_t>((val + 0x800LL) & ~0xfffLL);
            const uint32_t insn = rd32(loc);
            wr32(loc, (insn & 0xfffu) | (hi & 0xfffff000u));
            return LoadStatus::ok;
        }
        case r_riscv_lo12_i: {
            if ((offset & 3u) != 0u) return LoadStatus::reloc_out_of_range;
            const uint32_t lo = static_cast<uint32_t>(S + A) & 0xfffu;
            const uint32_t insn = rd32(loc);
            wr32(loc, (insn & 0x000fffffu) | (lo << 20));
            return LoadStatus::ok;
        }
        case r_riscv_lo12_s: {
            if ((offset & 3u) != 0u) return LoadStatus::reloc_out_of_range;
            const uint32_t lo = static_cast<uint32_t>(S + A) & 0xfffu;
            const uint32_t insn = rd32(loc);
            const uint32_t imm11_5 = (lo >> 5) & 0x7fu;
            const uint32_t imm4_0 = lo & 0x1fu;
            wr32(loc, (insn & 0x01fff07fu) | (imm11_5 << 25) | (imm4_0 << 7));
            return LoadStatus::ok;
        }
        case r_riscv_jal: {
            if ((offset & 3u) != 0u) return LoadStatus::reloc_out_of_range;
            const int64_t delta = (S + A) - static_cast<int64_t>(P);
            if ((delta & 1LL) != 0 || delta < -(1LL << 20) || delta >= (1LL << 20)) {
                return LoadStatus::reloc_out_of_range;
            }
            const uint32_t u = static_cast<uint32_t>(delta);
            const uint32_t b20 = (u >> 20) & 1u;
            const uint32_t b10_1 = (u >> 1) & 0x3ffu;
            const uint32_t b11 = (u >> 11) & 1u;
            const uint32_t b19_12 = (u >> 12) & 0xffu;
            const uint32_t insn = rd32(loc);
            wr32(loc, (insn & 0xfffu) | (b20 << 31) | (b10_1 << 21) | (b11 << 20) | (b19_12 << 12));
            return LoadStatus::ok;
        }
        case r_riscv_call:
        case r_riscv_call_plt: {
            if ((offset & 3u) != 0u || offset + 8u > target_size) return LoadStatus::reloc_out_of_range;
            const int64_t delta = (S + A) - static_cast<int64_t>(P);
            if (delta < -(1LL << 31) || delta >= (1LL << 31)) return LoadStatus::reloc_out_of_range;
            const int32_t hi = static_cast<int32_t>((delta + 0x800LL) & ~0xfffLL);
            const int32_t lo = static_cast<int32_t>(delta - hi);
            const uint32_t auipc = rd32(loc);
            const uint32_t jalr = rd32(loc + 4);
            wr32(loc, (auipc & 0xfffu) | (static_cast<uint32_t>(hi) & 0xfffff000u));
            wr32(loc + 4, (jalr & 0x000fffffu) | ((static_cast<uint32_t>(lo) & 0xfffu) << 20));
            return LoadStatus::ok;
        }
        default:
            return LoadStatus::unsupported_reloc;
    }
}

LoadStatus apply_arm_reloc(uint8_t* target_base, size_t target_size,
                           uint32_t offset, uint32_t type,
                           uintptr_t sym_addr, int32_t addend, bool is_rela) {
    if (offset + 4u > target_size) return LoadStatus::reloc_out_of_range;

    uint8_t* loc = target_base + offset;
    const uintptr_t P = reinterpret_cast<uintptr_t>(loc);
    const int64_t S = static_cast<int64_t>(sym_addr);

    switch (type) {
        case r_arm_abs32: {
            const int64_t A = is_rela ? static_cast<int64_t>(addend)
                                      : static_cast<int64_t>(static_cast<int32_t>(rd32(loc)));
            wr32(loc, static_cast<uint32_t>(S + A));
            return LoadStatus::ok;
        }
        case r_arm_rel32: {
            const int64_t A = is_rela ? static_cast<int64_t>(addend)
                                      : static_cast<int64_t>(static_cast<int32_t>(rd32(loc)));
            wr32(loc, static_cast<uint32_t>((S + A) - static_cast<int64_t>(P)));
            return LoadStatus::ok;
        }
        case r_arm_thm_call:
        case r_arm_thm_jump24: {
            if ((offset & 1u) != 0u) return LoadStatus::reloc_out_of_range;
            const uint16_t hw1 = rd16(loc);
            const uint16_t hw2 = rd16(loc + 2);
            // Decode implicit addend for SHT_REL (Thumb-2 PC is P + 4, so initial addend is -4).
            int64_t A = addend;
            if (!is_rela) {
                const uint32_t s_bit = (hw1 >> 10) & 1u;
                const uint32_t j1 = (hw2 >> 13) & 1u;
                const uint32_t j2 = (hw2 >> 11) & 1u;
                const uint32_t i1 = ~(j1 ^ s_bit) & 1u;
                const uint32_t i2 = ~(j2 ^ s_bit) & 1u;
                const uint32_t imm10 = hw1 & 0x3ffu;
                const uint32_t imm11 = hw2 & 0x7ffu;
                uint32_t raw = (s_bit << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1);
                if (s_bit) raw |= 0xfe000000u;
                A = static_cast<int64_t>(static_cast<int32_t>(raw));
            }
            const int64_t delta = ((S & ~1LL) + A) - static_cast<int64_t>(P);
            if (delta < -(1LL << 24) || delta >= (1LL << 24)) {
                return LoadStatus::reloc_out_of_range;
            }
            const uint32_t u = static_cast<uint32_t>(delta);
            const uint32_t s_bit = (u >> 24) & 1u;
            const uint32_t i1 = (u >> 23) & 1u;
            const uint32_t i2 = (u >> 22) & 1u;
            const uint32_t j1 = (~i1 ^ s_bit) & 1u;
            const uint32_t j2 = (~i2 ^ s_bit) & 1u;
            const uint32_t imm10 = (u >> 12) & 0x3ffu;
            const uint32_t imm11 = (u >> 1) & 0x7ffu;
            const uint16_t new_hw1 = static_cast<uint16_t>((hw1 & 0xf800u) | (s_bit << 10) | imm10);
            const uint16_t new_hw2 = static_cast<uint16_t>((hw2 & 0xd000u) | (j1 << 13) | (j2 << 11) | imm11);
            wr16(loc, new_hw1);
            wr16(loc + 2, new_hw2);
            return LoadStatus::ok;
        }
        case r_arm_movw_abs_nc:
        case r_arm_movt_abs: {
            if ((offset & 1u) != 0u) return LoadStatus::reloc_out_of_range;
            const uint16_t hw1 = rd16(loc);
            const uint16_t hw2 = rd16(loc + 2);
            // Decode implicit 16-bit signed addend encoded in imm4:i:imm3:imm8 for SHT_REL (AAELF32).
            int64_t A = static_cast<int64_t>(addend);
            if (!is_rela) {
                const uint32_t orig_imm4 = hw1 & 0xfu;
                const uint32_t orig_i = (hw1 >> 10) & 1u;
                const uint32_t orig_imm3 = (hw2 >> 12) & 0x7u;
                const uint32_t orig_imm8 = hw2 & 0xffu;
                const uint16_t orig_imm16 = static_cast<uint16_t>((orig_imm4 << 12) | (orig_i << 11) |
                                                                  (orig_imm3 << 8) | orig_imm8);
                A = static_cast<int64_t>(static_cast<int16_t>(orig_imm16));
            }
            const uint32_t val = static_cast<uint32_t>(S + A);
            const uint16_t imm16 = (type == r_arm_movt_abs)
                                       ? static_cast<uint16_t>((val >> 16) & 0xffffu)
                                       : static_cast<uint16_t>(val & 0xffffu);
            const uint16_t imm4 = static_cast<uint16_t>((imm16 >> 12) & 0xfu);
            const uint16_t i_bit = static_cast<uint16_t>((imm16 >> 11) & 1u);
            const uint16_t imm3 = static_cast<uint16_t>((imm16 >> 8) & 0x7u);
            const uint16_t imm8 = static_cast<uint16_t>(imm16 & 0xffu);
            wr16(loc, static_cast<uint16_t>((hw1 & 0xfbf0u) | (i_bit << 10) | imm4));
            wr16(loc + 2, static_cast<uint16_t>((hw2 & 0x8f00u) | (imm3 << 12) | imm8));
            return LoadStatus::ok;
        }
        default:
            return LoadStatus::unsupported_reloc;
    }
}

} // namespace

LoadStatus Loader::load(const uint8_t* elf_bytes, size_t elf_size,
                        uint8_t* arena, size_t arena_bytes,
                        const LoadConfig& config,
                        LoadedModule& out_module) {
    out_module = LoadedModule{};
    if (!elf_bytes || !arena || elf_size < 52u || arena_bytes == 0) {
        return LoadStatus::invalid_argument;
    }
    if (!config.sync_icache) {
        return LoadStatus::missing_icache_hook; // NXR-147: fail closed without cache publication.
    }

    // Validate ELF32 identification and header.
    if (elf_bytes[0] != 0x7fu || elf_bytes[1] != 'E' || elf_bytes[2] != 'L' || elf_bytes[3] != 'F') {
        return LoadStatus::bad_elf_header;
    }
    if (elf_bytes[4] != elfclass32 || elf_bytes[5] != elfdata2lsb || elf_bytes[6] != 1u) {
        return LoadStatus::bad_elf_header;
    }
    const uint16_t e_type = rd16(elf_bytes + 16);
    const uint16_t e_machine = rd16(elf_bytes + 18);
    const uint32_t e_shoff = rd32(elf_bytes + 32);
    const uint16_t e_shentsize = rd16(elf_bytes + 46);
    const uint16_t e_shnum = rd16(elf_bytes + 48);

    if (e_type != et_rel) return LoadStatus::bad_elf_header;
    if (e_machine != em_riscv && e_machine != em_arm) return LoadStatus::unsupported_machine;
    if (config.expected_arch != TargetArch::any &&
        static_cast<uint16_t>(config.expected_arch) != e_machine) {
        return LoadStatus::unsupported_machine;
    }
    if (e_shnum == 0 || e_shnum > max_sections || e_shentsize < 40u) {
        return LoadStatus::bad_section_table;
    }
    if (static_cast<size_t>(e_shoff) + static_cast<size_t>(e_shnum) * e_shentsize > elf_size) {
        return LoadStatus::bad_section_table;
    }

    out_module.state = SlotState::loading;
    out_module.arch = static_cast<TargetArch>(e_machine);
    out_module.arena_base = reinterpret_cast<uintptr_t>(arena);

    Shdr shdrs[max_sections]{};
    uintptr_t sec_addrs[max_sections]{};
    bool sec_loaded[max_sections]{};
    int symtab_idx = -1;

    for (uint16_t i = 0; i < e_shnum; ++i) {
        shdrs[i] = read_shdr(elf_bytes + e_shoff + static_cast<size_t>(i) * e_shentsize);
        if (shdrs[i].type == sht_symtab) symtab_idx = static_cast<int>(i);
    }

    // Pass 1: Place all SHF_ALLOC sections into caller-owned static arena.
    size_t cursor = 0;
    for (uint16_t i = 0; i < e_shnum; ++i) {
        const auto& s = shdrs[i];
        if ((s.flags & shf_alloc) == 0 || s.type == sht_null) continue;
        size_t align = s.addralign > 1u ? static_cast<size_t>(s.addralign) : 4u;
        if ((align & (align - 1u)) != 0) align = 4u;
        const uintptr_t base = reinterpret_cast<uintptr_t>(arena);
        const uintptr_t cur_addr = base + cursor;
        const size_t pad = (align - (cur_addr & (align - 1u))) & (align - 1u);
        if (pad > arena_bytes - cursor || s.size > arena_bytes - cursor - pad) {
            out_module.state = SlotState::faulted;
            return LoadStatus::arena_too_small;
        }
        cursor += pad;
        uint8_t* dst = arena + cursor;
        if (s.type == sht_nobits) {
            for (uint32_t b = 0; b < s.size; ++b) dst[b] = 0u;
        } else {
            if (static_cast<size_t>(s.offset) + s.size > elf_size) {
                out_module.state = SlotState::faulted;
                return LoadStatus::bad_section_table;
            }
            for (uint32_t b = 0; b < s.size; ++b) dst[b] = elf_bytes[s.offset + b];
        }
        sec_addrs[i] = reinterpret_cast<uintptr_t>(dst);
        sec_loaded[i] = true;
        cursor += s.size;
        ++out_module.sections_loaded;
    }
    out_module.arena_used = cursor;

    // Pass 2: Apply all SHT_REL and SHT_RELA relocations against allowlist + placed sections.
    for (uint16_t i = 0; i < e_shnum; ++i) {
        const auto& rs = shdrs[i];
        if (rs.type != sht_rel && rs.type != sht_rela) continue;
        const bool is_rela = (rs.type == sht_rela);
        const size_t entry_sz = is_rela ? 12u : 8u;
        if (rs.info >= e_shnum || !sec_loaded[rs.info]) continue;
        if (rs.link >= e_shnum || shdrs[rs.link].type != sht_symtab) {
            out_module.state = SlotState::faulted;
            return LoadStatus::bad_section_table;
        }
        const auto& symtab = shdrs[rs.link];
        if (symtab.link >= e_shnum) {
            out_module.state = SlotState::faulted;
            return LoadStatus::bad_section_table;
        }
        const auto& strtab = shdrs[symtab.link];
        const size_t sym_stride = symtab.entsize >= 16u ? symtab.entsize : 16u;
        const size_t sym_count = symtab.size / sym_stride;

        if (static_cast<size_t>(rs.offset) + rs.size > elf_size ||
            static_cast<size_t>(symtab.offset) + symtab.size > elf_size) {
            out_module.state = SlotState::faulted;
            return LoadStatus::bad_section_table;
        }

        const size_t rel_stride = rs.entsize >= entry_sz ? rs.entsize : entry_sz;
        const size_t rel_count = rs.size / rel_stride;
        auto* target_ptr = reinterpret_cast<uint8_t*>(sec_addrs[rs.info]);
        const size_t target_size = shdrs[rs.info].size;

        for (size_t r = 0; r < rel_count; ++r) {
            const uint8_t* rp = elf_bytes + rs.offset + r * rel_stride;
            const uint32_t r_offset = rd32(rp + 0);
            const uint32_t r_info = rd32(rp + 4);
            const int32_t r_addend = is_rela ? static_cast<int32_t>(rd32(rp + 8)) : 0;
            const uint32_t sym_idx = r_info >> 8;
            const uint32_t r_type = r_info & 0xffu;

            if (e_machine == em_riscv && r_type == r_riscv_relax) {
                ++out_module.relocations_applied;
                continue;
            }

            if (sym_idx >= sym_count) {
                out_module.state = SlotState::faulted;
                return LoadStatus::unresolved_symbol;
            }
            const Sym sym = read_sym(elf_bytes + symtab.offset + sym_idx * sym_stride);
            const char* sym_name = safe_str(elf_bytes, elf_size, strtab, sym.name);
            uintptr_t sym_addr = 0;
            const LoadStatus rstat = resolve_symbol(sym, sym_name, sec_addrs, sec_loaded, e_shnum,
                                                    config, sym_addr);
            if (rstat != LoadStatus::ok) {
                out_module.state = SlotState::faulted;
                return rstat;
            }

            const LoadStatus astat = (e_machine == em_riscv)
                ? apply_riscv_reloc(target_ptr, target_size, r_offset, r_type, sym_addr, r_addend)
                : apply_arm_reloc(target_ptr, target_size, r_offset, r_type, sym_addr, r_addend, is_rela);
            if (astat != LoadStatus::ok) {
                out_module.state = SlotState::faulted;
                return astat;
            }
            ++out_module.relocations_applied;
        }
    }

    // Pass 3: Locate entry symbol if requested.
    if (config.entry_symbol && config.entry_symbol[0] != '\0') {
        bool found = false;
        if (symtab_idx >= 0) {
            const auto& symtab = shdrs[symtab_idx];
            if (symtab.link < e_shnum &&
                static_cast<size_t>(symtab.offset) + symtab.size <= elf_size) {
                const auto& strtab = shdrs[symtab.link];
                const size_t stride = symtab.entsize >= 16u ? symtab.entsize : 16u;
                const size_t count = symtab.size / stride;
                for (size_t i = 0; i < count; ++i) {
                    const Sym sym = read_sym(elf_bytes + symtab.offset + i * stride);
                    const char* name = safe_str(elf_bytes, elf_size, strtab, sym.name);
                    if (name && cstr_eq(name, config.entry_symbol) &&
                        sym.shndx < e_shnum && sec_loaded[sym.shndx]) {
                        out_module.entry_address = sec_addrs[sym.shndx] + sym.value;
                        found = true;
                        break;
                    }
                }
            }
        }
        if (!found) {
            out_module.state = SlotState::faulted;
            return LoadStatus::entry_not_found;
        }
    }

    // NXR-147: Flush/invalidate caches before marking module executable/ready.
    config.sync_icache(out_module.arena_base, out_module.arena_used, config.sync_ctx);
    out_module.state = SlotState::ready;
    return LoadStatus::ok;
}

void Loader::unload(LoadedModule& mod, uint8_t* arena, size_t arena_bytes) {
    if (arena && arena_bytes > 0) {
        for (size_t i = 0; i < arena_bytes; ++i) arena[i] = 0u;
    }
    mod = LoadedModule{};
}

} // namespace rtos::elf
