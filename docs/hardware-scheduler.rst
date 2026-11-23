Hardware Scheduler & Flagship Co-Design
=======================================

NexRT supports an optional, statically verified hardware/software co-design
pipeline targeting the fault-tolerant **RISCue** RV32IM soft core. Every phase
preserves the strict zero-heap, zero-constructor static boot contract.

Phase 1: Core Kernel Hardening
------------------------------

* **Voluntary yield vs. priority reconsideration**: ``rtos::yield()`` sets an
  internal ``yield_requested`` flag so equal-priority rotation occurs only on
  explicit yields or timer ticks with ``RTOS_ROUND_ROBIN=1``. Wakeups from
  ``Semaphore::release()``, ``Queue::send()``, ``Mutex::unlock()``,
  ``EventFlags::set()``, and ``rtos::set_priority()`` reconsider priorities
  without rotating equal-priority peers.
* **Starvation-free per-priority round-robin**: ``Scheduler`` maintains a
  bounded ``order[RTOS_MAX_THREADS]`` permutation rather than a single shared
  cursor, preventing periodic high-priority preemption from starving equal
  lower-priority threads.
* **External IRQ dispatch & deferred reschedule**: ``rtos::register_external_irq``
  registers a boot-static peripheral handler; ``schedule_ready()`` latches
  ``reschedule_pending`` in interrupt context so woken higher-priority threads
  preempt immediately upon trap exit.
* **Dedicated RISC-V trap stack**: ``ports/riscue/context.S`` switches ``sp``
  to ``mscratch`` (``_stack_top``) before calling ``rtos_trap``, isolating
  kernel C++ frames from thread stacks just like ARM's MSP/PSP split.

Phase 2: Fault Containment & WCET Watchdog
------------------------------------------

* **Hardware stack guard** (``RTOS_HW_PROTECT=1``): configures a 32-byte
  read-only guard at the bottom of the active thread stack (ARMv7-M MPU Region 0
  with ``AP=0b101``; RISC-V PMP Region 0 NAPOT) so stack overflows trap
  synchronously on the first bad store while preserving read access for
  ``guard_ok()`` and ``stack_usage()``.
* **WCET budget watchdog** (``RTOS_BUDGET_WATCHDOG=1``):
  ``rtos::set_execution_budget()`` bounds continuous execution cycles per
  thread. Overruns transition the thread to ``State::quarantined``, abandon
  owned mutexes with ``Result::owner_dead``, and invoke the optional supervisor
  hook registered via ``rtos::register_budget_fault_hook()``.

Phase 3: NexRT-HWSched v1 Peripheral
------------------------------------

``rtl/nexrt_hw_sched.sv`` and ``rtl/nexrt_hw_sched_axi.sv`` implement a
synthesizable 16-thread hardware scheduler peripheral (version ``0x4E585301``)
with:

* 1-cycle combinational priority + starvation-free per-priority round-robin
  arbiter.
* 16 parallel 64-bit hardware deadline comparators against ``mtime`` for true
  tickless operation.
* 4 hardware peripheral IRQ-to-thread wake routes (1-cycle wake).
* Per-thread hardware residency counters, dispatch counters, and WCET budget
  watchdogs.
* Triple-Modular Redundancy (``TMR_ENABLE=1``) with automatic 1-cycle scrubbing,
  a saturating ``SEU_COUNT`` register, and fault-injection hooks verified in
  lockstep against ``rtos::hw_sched::Model`` via Verilator.

Phase 4: RISCue-MT Banked Hardware Contexts
-------------------------------------------

``rtl/riscue_banked_rf.sv`` and ``ports/riscue/context_banked.S`` provide
``NUM_BANKS=8`` TMR-protected hardware register banks (``x1..x31``, ``mepc``,
``mstatus``) indexed directly by ``NexRT-HWSched``'s ``active_tid``. Switching
between banked real-time threads skips all 72 AXI memory transfers, while
threads with ``tid >= NUM_BANKS`` assert ``spill_required`` and fall back
transparently to standard RAM frames.

Phase 5: Flagship StateForge Demo
---------------------------------

``examples/flagship_stateforge.cpp`` combines StateForge deterministic
transitions, priority-inheriting device transactions, external IRQ wakeup,
WCET budgets, and bounded JSON telemetry. ``sim/rtl/riscue_flagship_soc.sv``
wires unchanged ``riscue_pipeline_core`` (``RF_TMR=1, FI_ENABLE=1``) to
``nexrt_hw_sched_axi``.
