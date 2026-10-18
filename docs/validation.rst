Local validation results
========================

Engineering baseline checked 18 October 2026 on Ubuntu-24.04 through WSL.
Native compiler: GCC 13.3.0. RISC-V compiler: GCC 13.2.0, RV32IM+Zicsr,
ILP32. Documentation: Sphinx 8.2.3 with Python 3.12.3. Host automation:
.NET SDK 10.0.402, C# with generated XDoc XML documentation.

Completed checks
----------------

* Default host preset: 4/4 tests passed (C# tooling, scheduler, Ethernet, freestanding memory).
* Minimal host preset: 3/3 tests passed (C# tooling, scheduler and freestanding memory).
* Default RISCue firmware: linked successfully; allocator-symbol audit passed.
* Minimal RISCue firmware: linked successfully; allocator-symbol audit passed.
* Static contract fixtures: constant initialization accepted; runtime constructor
  rejected by linker with section garbage collection enabled; reachable allocator
  rejected by the final symbol audit.
* Custom audit and fixture automation migrated from Python to C#; tool build
  completed with zero compiler warnings/errors and emitted Rtos.Tools.xml.
* Strict Sphinx HTML build using nitpicky mode and warnings-as-errors: passed.
* Five sequential embedded-systems review passes: completed; findings and fixes
  are recorded in the review chapter.
* Debugger snapshots: ELF symbols ``rtos_thread_info`` and ``rtos_system_info``
  confirmed present; snapshot implementation is linked into the demo.

Firmware sizes
--------------

.. list-table:: GNU size output, bytes
   :header-rows: 1

   * - Profile
     - text
     - data
     - bss
     - Total
   * - Default with diagnostic snapshot example
     - 4664
     - 84
     - 4000
     - 8748
   * - Minimal with diagnostic snapshot example
     - 4380
     - 80
     - 3936
     - 8396

These totals include example objects and stacks but exclude the linker-reserved
4 KiB boot stack. The optional Ethernet archive compiles in the default firmware
build but is not linked into this thread-only demo. The default metrics snapshot
array remains present in minimal too, so these sizes do not represent an absolute
smallest possible application. Both firmware links report the expected RWX RAM
load segment; the platform has no permission isolation.

Stack evidence
--------------

GCC stack-usage reports for the default build identify 64-byte frames for the
slow task and snapshot function, 32 bytes for sleep_for and rtos_trap, and zero
for the freestanding byte operations. The trap assembly separately consumes
144 bytes. The snapshot output array is static rather than automatic storage.
These reports are consistent with the example's 1024-byte thread stacks but
are not a complete worst-case stack bound: compiler integer helpers, every
call path, instruction-level preemption placement, and hardware stress still
need analysis and testing.

Artifacts and limits
--------------------

Generated firmware, maps, and .su reports are under ``build/riscue`` and
``build/riscue-minimal``. CTest records are in each host preset's
``Testing/Temporary/LastTest.log``. The strict documentation log is
``build/docs-build.log`` and the delivered manual is ``build/manual/html``.
Negative fixture sources/images are in ``build/riscue/static-contract``.

No RISCue RTL or physical-board execution was performed for this baseline.
Register restoration, interrupt timing, real snapshot accounting, API failure
paths, and stack fault handling therefore remain target release gates. Hardware
Ethernet, PHY, DMA and TCP/IP integration remain pending. No certification or
blanket application/third-party leak-freedom claim is made.
