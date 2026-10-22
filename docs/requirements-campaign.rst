Requirements acceptance campaign
================================

The 21 October 2026 campaign contains 200 authored acceptance iterations,
each with a distinct identity, requirement link, scenario and test function.
Ten iterations form a milestone. Every milestone configures and builds the host
project, runs the cumulative CTest suite, records evidence, and creates a local
Git commit only after passing. No push is performed.

``requirements/campaign-200/iterations.tsv`` records all 200 identities and test
source hashes. ``checkpoints/01`` through ``checkpoints/20`` retain build, test,
case and parent-commit logs. Each case can also run individually::

   build/host/campaign_16 --case 153

The C# orchestrator is ``tools/NexRT.Campaign``. Its milestone argument is 1..20;
it refuses missing/out-of-order prior milestones and completed milestones.
The completed ledger is historical evidence and should not be deleted to rerun
it. Use ordinary CMake/CTest to reverify the current sources::

   cmake -S . -B build/host -G Ninja -DRTOS_INTERPRETER=ON
   cmake --build build/host -j2
   ctest --test-dir build/host --output-on-failure

Coverage and closure scope
--------------------------

.. list-table:: Authored iterations
   :header-rows: 1

   * - Range
     - Acceptance area
   * - 1--40
     - Periodic release grid, binary trace, first-failure capture, typed MMIO
   * - 41--70
     - UART input validation, queued transport and bounded formatting
   * - 71--100
     - DMA binding, receive ownership, completion, quarantine and recovery
   * - 101--140
     - Copied queues, wait lifecycle, ISR boundaries, semaphores and events
   * - 141--180
     - Mutex lifecycle, inheritance, scheduler order, logging and DMA aliasing
   * - 181--200
     - Executable loading and diagnostic-symbol integrity

The acceptance scope is host execution. The tracker reconciles 18 previously
planned entries with implemented source and directed evidence (96 planned at
campaign start, 78 at completion). Its status remains PartialSourceUnverified
where target qualification or other acceptance clauses remain open. Test-case
closure does not automatically close an entire requirement. Physical interrupt,
DMA/cache, reset retention, hardware fault and worst-case timing qualification
require their corresponding target evidence.

Defect regressions
------------------

* UART now validates null nonempty sources, representable counts, alignment and
  register-range overflow before MMIO. Empty writes touch no device.
* The scheduler's initial round-robin cursor now selects the first registered
  slot independently of configured capacity. Case 153 retains its failing log.
* The ELF loader enforces segment alignment/congruence and requires the entry
  point in loaded executable memory. Milestone 19 retains the failing test.
* Diagnostic symbols must use an actual string table. Local names cannot hide
  global status symbols, and conflicting global definitions are rejected.
  Milestone 20 retains failures for string-table, local-shadow and global-conflict
  cases before the fixes.

New optional helpers
--------------------

``rtos_periodic.hpp`` computes future releases from the original grid, reports
missed periods, rejects ambiguous half-range observations and saturates its miss
counter. It does not add absolute sleep to the kernel; callers own sleep policy.

``rtos_trace.hpp`` provides a boot-static binary ring with explicit drop-new
overflow accounting. ``rtos_fault_record.hpp`` captures the first failure and
copies coherent snapshots under the single-hart mask. Neither claims reset
retention or NMI/double-fault safety. Both require the synchronization provider.

``rtos_mmio.hpp`` provides read-only, write-only, read/write and W1C word policies.
Forbidden operations and unaligned addresses do not touch the bus. W1C refuses
read-modify-write. The BSP remains responsible for valid device addresses,
cache policy, ordering, faults and shared-device ownership.

``tests/campaign/firmware_probe.cpp`` exercises the new helpers in actual RISCue
firmware and reports ``campaign helpers PASS`` through the simulated UART. It is
a validation application, separate from the three-worker greeting demo.
