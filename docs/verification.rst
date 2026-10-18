Verification and qualification record
=====================================

Evidence policy
---------------

This baseline is an engineering prototype. A professional-style manual and
review record do not confer certification or replace independent assessment.
Choose the applicable assurance standard and project safety requirements
before making compliance claims. The evidence below supports development;
target tests and timing analysis remain release gates for deployment.

Requirements and traceability
-----------------------------

.. list-table:: Implemented requirements and evidence
   :header-rows: 1
   :widths: 12 38 25 25

   * - ID
     - Requirement
     - Implementation
     - Verification
   * - K-001
     - Highest ready priority wins; equal priorities rotate when requested.
     - Scheduler::pick
     - Host priority/rotation/no-rotation tests
   * - K-002
     - Sleep deadlines support tick wrap and bounded relative delay.
     - sleep_for; Scheduler::advance
     - Host wrap/wake tests; target sleep API pending
   * - K-003
     - Timer catch-up scans once, not once per missed tick.
     - elapsed_ticks; rtos_trap
     - Host arithmetic tests; target masked-interrupt test pending
   * - K-004
     - Application registry is bounded, unique, and sealed at start.
     - create; Scheduler::add
     - Host capacity/duplicate tests; target boot seal pending
   * - K-005
     - Preempted integer/CSR state is preserved.
     - context.S; initialize
     - ABI source review and cross-build; target register test pending
   * - K-006
     - Kernel has no general heap allocation or dynamic C++ initialization.
     - Static objects; linker ASSERT; C# Rtos.Tools audit
     - Default/minimal ELF audits; negative allocation/constructor fixtures
   * - K-007
     - Coherent thread metrics include priority, state, and runtime residency.
     - snapshot; account
     - Source review/cross-build; target monotonic and conservation tests pending
   * - K-008
     - Idle is fallback only and does not consume application capacity.
     - Separate idle storage; trap fallback
     - Source review; target priority-zero/zero-thread tests pending
   * - K-009
     - Stack/control-block overlap is rejected; optional sentinel detects corruption.
     - create; guard_ok
     - Host range helper tests/source review; target fault injection pending
   * - N-001
     - Ethernet adapter uses fixed storage and bounded RX attempt count.
     - Ethernet::poll
     - Host budget/empty/busy/error/drop tests
   * - N-002
     - TX consumes/copies synchronously and reports backpressure.
     - Mac contract; Ethernet::send
     - Host invalid-length/busy/error tests; board DMA ownership test pending
   * - B-001
     - Feature combinations compile and default/minimal behavior is testable.
     - CMake presets; config assertions
     - Host default/minimal tests; cross-build both profiles
   * - B-002
     - Freestanding byte operations preserve copy/fill and overlap semantics.
     - memory.cpp
     - Host memset/memcpy/memmove/memcmp tests; cross-build
   * - D-001
     - Manual builds with Sphinx warnings treated as errors.
     - docs/conf.py; docs target
     - Strict HTML build

Source review is not runtime test coverage. Host tests do not execute kernel
inline assembly, interrupt masking, MMIO, snapshots, priority API, or startup.
Each pending target test must be recorded with firmware version, compiler flags,
RISCue architecture/protection variant, clock, seed/workload, result, and logs.

Reproducible checks
-------------------

.. code-block:: sh

   cmake --preset host
   cmake --build --preset host
   ctest --preset host
   cmake --preset minimal
   cmake --build --preset minimal
   ctest --preset minimal
   cmake --preset riscue -DRISCV_TOOLCHAIN_PREFIX=/your/toolchain/prefix-
   cmake --build --preset riscue
   cmake --build --preset riscue --target static_contract_check
   # Strict documentation command is in the getting-started chapter.

The post-link audit scans demangled symbols for known C/C++ allocators,
deallocation, and initialization helpers. Linker KEEP plus ASSERT rejects
constructor arrays even if garbage collection would otherwise remove them.
This audit is intentionally conservative: an allocator symbol linked but never
called still fails. Custom allocators and third-party ownership errors can evade
symbol checks and require independent review.

Release gates on RISCue
-----------------------

#. Repeatedly preempt register-stress threads with distinct patterns in every
   integer register and verify stack alignment, PC continuation, and MIE restoration.
#. Exercise zero threads, full capacity, priority zero, strict priority starvation,
   equal-priority rotation, disabled round-robin, runtime priority changes,
   sleep return, thread return, and explicit exit.
#. Mask interrupts for multiple periods and verify catch-up, monotonic time,
   compare writes, and sleeper wakeup around tick wrap.
#. Take metrics snapshots during activity; verify monotonic residency and interval
   totals, zero-valued disabled counters, removal of accounting code, truncation,
   and idle utilization.
#. Inject guard corruption, invalid clock configuration, unexpected traps, overlap,
   post-start create, and exhausted registration; verify documented failure behavior.
#. Establish worst-case trap/critical-section latency and stack depth from exact
   compiled code and stress tests. Compiler .su output alone is incomplete evidence.
#. For Ethernet, verify MAC/PHY link changes, DMA coherency, descriptor ownership,
   saturation, bounded queue exhaustion, malformed traffic, and protocol cleanup.
#. Run on each supported RISCue architecture/protection configuration before
   claiming coverage of those variants. No RTL variants were modified here.

Change control
--------------

Changes to frame offsets, compiler ABI, privilege mode, timer layout, locking,
or configuration ABI require renewed port review and target tests. Update this
matrix, configuration reference, and build evidence whenever behavior changes.
Keep generated logs in build/ and archive release evidence with the reviewed
source revision. Hardware validation, independent review, and formal qualification
remain separate activities from the five agent-assisted review iterations.
