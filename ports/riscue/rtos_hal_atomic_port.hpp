#pragma once
/** @file
 * @brief RISCue RV32 Zaamo adapter for an explicitly capable native RAM endpoint.
 * The pinned assembler lacks the named Zaamo subset; emit reviewed encodings
 * without enabling the full A extension or compiler-generated LR/SC sequences.
 */
#include <stdint.h>
#if !defined(__riscv) || __riscv_xlen!=32
#error "The RISCue Zaamo adapter requires RV32"
#endif
namespace rtos::hal::atomic {
/** @brief Check whether this RISCue core advertises enabled Zaamo instructions.
 * CSR 0xfc0 is a CSR number, not a RAM/MMIO address. Bit 0 is the platform's
 * feature flag; misa.A stays clear because this implementation has no LR/SC.
 * Query only on a core implementing this custom CSR: an older core may trap.
 * This does not probe a particular address or prove that its RAM supports AMOs.
 * The read neither modifies interrupt state nor acts as a memory-order fence.
 */
inline bool available(){
    uint32_t bits;
    // csrr is CSRRS rd,csr,x0: read into a compiler-selected register, with no
    // CSR write because rs1=x0. There is no memory operand or interrupt change.
    asm volatile("csrr %0, 0xfc0":"=r"(bits));
    return (bits&1)!=0;
}
/** @brief Atomically update one supported RAM word and return its old contents.
 * @tparam Operation ISA funct5: add=0, swap=1, XOR=4, OR=8, AND=12.
 * @param word Four-byte-aligned writable normal RAM on the capable endpoint.
 * @param value The rs2 operand; addition wraps modulo 2^32.
 * @return The word read at the atomic endpoint's update/linearization point.
 * The caller guarantees valid storage/lifetime and the platform RAM contract.
 * A bad alignment, unsupported region or endpoint fault may cause a CPU trap;
 * this helper does not catch exceptions, spin, mask interrupts or retry writes.
 * In particular, an operation that committed before reset must not be blindly
 * repeated: atomicity does not provide exactly-once execution across resets.
 */
template<unsigned Operation> inline uint32_t apply(uint32_t* word,uint32_t value){
    static_assert(Operation==0||Operation==1||Operation==4||Operation==8||Operation==12);
    uint32_t old;
    // .insn r operands are opcode, funct3, funct7, rd, rs1, rs2.
    // opcode=0x2f selects AMO; funct3=2 selects a 32-bit word. funct7 packs
    // funct5 in bits 6:2, aq in bit 1, rl in bit 0, hence (Operation<<2)|3.
    // %0 is rd (old word), %1 is rs1 (address), %2 is rs2 (input value),
    // and %3 is the compile-time funct7 immediate, not another CPU register.
    // '=r' needs no early-clobber: this single instruction consumes both source
    // registers before producing rd, so rd may legally alias rs1 or rs2.
    // volatile retains the operation even when the returned value is discarded.
    // The 'memory' clobber prevents compiler motion across the instruction;
    // aq/rl supply hardware acquire/release ordering in the accessed domain.
    // Neither mechanism performs cache maintenance or orders an unrelated I/O
    // domain; the BSP must provide any needed device fences separately.
    asm volatile(".insn r 0x2f, 2, %3, %0, %1, %2"
        : "=r"(old) : "r"(word),"r"(value),"i"((Operation<<2)|3) : "memory");
    return old;
}
/// AMOSWAP.W.aqrl: write value, returning the old word; also used for store().
inline uint32_t exchange(uint32_t* word,uint32_t value){return apply<1>(word,value);}
/// AMOADD.W.aqrl: write old+value modulo 2^32, returning old rather than the sum.
inline uint32_t fetch_add(uint32_t* word,uint32_t value){return apply<0>(word,value);}
/// AMOOR.W.aqrl: set the selected bits and return the complete pre-update word.
inline uint32_t fetch_or(uint32_t* word,uint32_t value){return apply<8>(word,value);}
/// AMOAND.W.aqrl: retain bits selected by value, returning the pre-update word.
inline uint32_t fetch_and(uint32_t* word,uint32_t value){return apply<12>(word,value);}
}
