#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nexrt::rv {
/// Independent CPU choices. The default preserves the RISCue M-only profile.
struct Profile {
    bool compressed=true, bitmanip=true, zaamo=false, user_pmp=false;
    bool wfi_sleep=false; // Explicit functional-model option; RISCue WFI is a NOP.
    uint32_t cycles_per_instruction=1;
    size_t ram_bytes=32768;
};
/// Architectural traps are guest events, never C++ exceptions on the hot path.
struct Trap { uint32_t cause=0, value=0, pc=0; uint64_t sequence=0; };
/// One RV32 hart with private RAM, CLINT and four programmable PMP entries.
/// This is an instruction model, not a pipeline/cycle-accurate hardware model.
class Machine {
public:
    explicit Machine(Profile profile={});
    Profile profile;
    std::vector<uint8_t> ram;
    std::array<uint32_t,32> x{};
    uint32_t pc=0, mstatus=0x1800, mie=0, mtvec=0, mepc=0, mcause=0, mtval=0, mscratch=0;
    uint32_t mcounteren=0, pmpcfg=0;
    std::array<uint32_t,4> pmpaddr{};
    uint64_t cycles=0, retired=0, mtime=0, mtimecmp=UINT64_MAX;
    uint64_t steps=0, timer_interrupts=0, software_interrupts=0, ecalls=0, amo_operations=0, skipped_cycles=0;
    bool user=false, msip=false, external_irq=false;
    Trap last_trap{}, last_fault{};
    uint64_t faults=0; // Sticky count: batch observation cannot lose an earlier fault.
    uint32_t last_pc=0,last_raw=0,last_rd=0,last_result=0;
    bool last_retired=false;
    uint64_t uart_writes=0; uint8_t last_uart=0;
    /// Optional host observer called only for a committed UART TX register write.
    void (*uart_sink)(void*,uint8_t)=nullptr;
    void* uart_context=nullptr;
    /// Execute one precise instruction or interrupt entry. No allocation occurs.
    void step();
    /// Run an instruction-boundary batch; interrupts remain checked every step.
    void run(uint64_t count);
    /// Host loader/observer access bypasses guest PMP. Bounds are still checked.
    uint32_t peek32(uint32_t address) const;
    void poke32(uint32_t address,uint32_t value);
    /// Lowest matching entry must cover the entire byte range. Overflow denies.
    bool permits(uint32_t address,unsigned bytes,bool read,bool write,bool execute) const;
    /// Instruction expansion is public for exhaustive independent decoder tests.
    static bool decompress(uint16_t raw,uint32_t& expanded);
private:
    // Raw bytes are checked on every fetch, so stores into code never leave a
    // stale decoded instruction. Cache entries allocate only at construction.
    struct Cached { uint32_t raw=0,insn=0; uint8_t length=0; };
    std::vector<Cached> cache;
    /// Save faulting PC/privilege, disable M interrupts and enter the M handler.
    void trap(uint32_t cause,uint32_t value);
    /// Combine level-sensitive software, timer and external interrupt sources.
    uint32_t pending() const;
    /// Check alignment/PMP before reading RAM or the supported MMIO registers.
    bool load(uint32_t address,unsigned bytes,uint32_t& value,bool execute=false);
    /// Check the entire access before any RAM or MMIO side effect takes place.
    bool store(uint32_t address,unsigned bytes,uint32_t value);
    /// Return false for an absent CSR; the decoder raises an illegal instruction.
    bool csr_read(uint32_t address,uint32_t& value) const;
    /// Apply supported WARL masks and PMP locks after decoder permission checks.
    void csr_write(uint32_t address,uint32_t value);
    /// MPRV borrows MPP only for data accesses; instruction privilege is unchanged.
    bool effective_user(bool execute) const;
};
}
