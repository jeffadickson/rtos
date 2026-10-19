Classic MicroBlaze port work package
================================================================================

Status: compiler/BSP handoff scaffold; context switch, vectors, timer/INTC adapter
and linker/startup are not implemented. Firmware remains disabled in the catalogue.
This directory must not contain copied RISC-V instructions disguised as a port.

The native configurator validates ``config/boards/microblaze.example.json`` after
the missing hardware values are supplied. ``cmake/microblaze-toolchain.cmake``
requires the real compiler prefix, CPU feature flags and endianness from Vitis.
No SDK/compiler is installed or presumed by that file. MicroBlaze V is a separate
RISC-V CPU and is rejected by this classic-ISA hardware contract.

Architecture implementation order
--------------------------------------------------------------------------------

1. Export the exact Vivado hardware: core version, endian mode, vectors, BRAM,
   AXI timer, interrupt-controller base/vector and synthesized timer clock.
2. Define a native MicroBlaze saved-frame ABI covering r1..r31, MSR and a canonical
   resume PC. Preserve r2/r13 small-data anchors and distinguish r14 interrupt,
   r15 subroutine and r17 exception conventions. Do not reuse RV32 word indices.
3. Implement separate interrupt and software-yield entry paths. Hardware interrupt
   entry disables IE and writes r14; RTID re-enables IE after its delay slot.
   A caller yielding with IRQs masked must resume masked until its C++ lock
   unwinds, so unconditional RTID is not a correct generic restore path.
4. Add acknowledged AXI timer/INTC dispatch with bounded missed-tick accounting,
   startup/BSS initialization and an exported BRAM linker map/overflow check.
5. Cross-build with the actual SDK, then run integer-register/PC/IE, priority,
   timeout, stack and no-heap tests on the selected FPGA design.

Authoritative starting points are AMD UG984's register conventions, interrupt
entry and RTID pages, linked in the Sphinx port bring-up chapter. Cache, optional
MMU/FPU, exception mode and low-latency interrupt options require explicit policy.
