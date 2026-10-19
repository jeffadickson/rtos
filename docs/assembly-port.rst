RISCue assembly and machine-state contract
================================================================================

These listings include the actual commented source with line numbers. The port
is RV32IM/Zicsr, ILP32, single-hart machine mode. Nested traps, floating-point,
vector state, TLS and user mode are unsupported. Physical/RTL preservation tests
remain pending. Comments explain the implemented ABI, not hardware qualification.

Reset and linker assumptions
--------------------------------------------------------------------------------

The loader places code, constants and initialized data at their linked RAM
addresses. Startup does not copy data from ROM, invoke runtime constructors,
initialize TLS or supply argc/argv. The linker rejects constructor sections,
aligns BSS bounds and stack top to 16 bytes, and reserves the upper 4096 bytes
for bootstrap stack use. Reset must already enter machine mode with writable
RAM accessible. Board boot policy must hold other harts out. Faults before vector
installation have no recovery handler.

.. literalinclude:: ../ports/riscue/start.S
   :language: text
   :linenos:
   :caption: Every startup instruction and directive

``.option norelax`` and linker ``--no-relax`` preserve gp initialization: a
relaxed ``la gp`` must not rely on an already initialized global pointer. BSS is
cleared in aligned words to its exclusive linker end. Normal main calls the
non-returning scheduler start; returning instead enters the final WFI loop.

Saved-frame ABI
--------------------------------------------------------------------------------

.. list-table:: 36 RV32 words, 144 bytes total
   :header-rows: 1

   * - Byte offset
     - Saved state and restore rule
   * - 0
     - Unused x0 slot; hardware zero is neither stored nor loaded.
   * - 4
     - x1 / ra, independent of the C++ handler call's return address.
   * - 8
     - x2 / original sp, restored last after all frame-relative loads.
   * - 12, 16
     - x3 / gp and x4 / tp; preserving tp does not implement TLS.
   * - 20..124
     - x5..x31 at offset 4 times register number, caller- and callee-saved alike.
   * - 128
     - mepc; only synchronous ECALL advances this PC by four bytes.
   * - 132
     - mstatus; MIE stays clear while restoring, and mret copies MPIE to MIE.
   * - 136..143
     - Alignment padding; initial frames zero it, traps need not write it.

``initialize_context`` must match this table exactly. Initial saved sp is the
stack top, PC is the entry trampoline, and ``0x1880`` means MPP=3 (machine),
MPIE=1 and MIE=0. First mret enables IRQs only after register restoration. The
trampoline calls the application's entry/argument and exits if that entry returns.

Trap entry, C++ handoff and return
--------------------------------------------------------------------------------

Hardware records mepc/mcause, copies MIE to MPIE and clears MIE. Assembly reserves
its frame on the interrupted stack and saves t0 before using it as scratch.
Asynchronous preemption must preserve caller-saved registers too, unlike a normal
C++ call. ``a0=frame`` and ``a1=mcause`` are the C ABI arguments to ``rtos_trap``.
The ordinary C++ handler stack is below the saved frame and must unwind before
assembly changes sp. The returned a0 selects the next frame. First dispatch has
no current thread and discards the bootstrap frame for a synthesized entry frame.

.. literalinclude:: ../ports/riscue/context.S
   :language: text
   :linenos:
   :caption: Every save, CSR access, handoff and restore instruction

The selected frame supplies mepc/mstatus before general-register restoration.
sp is restored last. mret resumes the selected machine-mode continuation and IRQ
state. Cause 11 is machine ECALL; its 32-bit instruction is skipped. Timer cause
``0x80000007`` retains the interrupted PC. Other causes halt in C++.

Inline assembly and ordering
--------------------------------------------------------------------------------

.. literalinclude:: ../ports/riscue/rtos_port.hpp
   :language: cpp
   :linenos:
   :caption: Annotated CSR, timer, frame initialization and ECALL functions

``=r`` is a compiler-allocated output register and ``r`` an allocated input.
``volatile`` keeps the assembly operation from being removed. The ``memory``
clobber prevents compiler movement of RAM accesses; it is not a hardware fence,
cache operation or SMP lock. Timer reads use high/low/high to detect rollover;
compare writes use high=max/low/high to avoid transient early interrupts. The
board must guarantee ordered 32-bit MMIO; another CPU/interconnect needs its
own verified device-ordering implementation.

A blocked caller may resume with MIE still clear until its lock destructor
restores saved state. Application critical sections must not protect shared data
across yield/sleep: another thread can run. Only MTIE is enabled. ISR APIs do not
install a peripheral vector, acknowledge a controller or qualify external IRQs.

When changing the port
--------------------------------------------------------------------------------

Change the frame constructor, assembly offsets and ABI table together. Inspect
disassembly and stack-usage reports, then test every integer register, PC, stack
alignment, IRQ restoration, missed ticks and ECALL return on hardware/RTL.
Budget the 144-byte frame plus C++ handler call chains on each thread's stack;
the bootstrap reserve is not a separate runtime interrupt stack.
