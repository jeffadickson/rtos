Optional RISCue hardware atomics
================================================================================

``RTOS_ZAAMO`` defaults OFF and is independent of the processor HAL. The normal
RISCue and Cortex-M3 ports retain the existing single-CPU implementation. Enable
the optional word-RMW backend only for RISCue firmware with synchronization and
an explicitly atomic-capable RAM integration::

   -DRTOS_ZAAMO=ON -DRTOS_SYNC=ON -DRTOS_ZAAMO_CAPABLE_RAM=ON

CMake rejects hardware mode for host/other-processor builds or without the RAM
contract. The existing generic AXI4-Lite wrapper is not atomic-capable. Selecting
the option does not replace that bridge, silently enable atomics in another
build or add the full RISC-V A extension to compiler flags.

API and HAL boundaries
----------------------

``AtomicU32`` remains a four-byte aligned, boot-static RAM word with the same
public API and no allocation. Hardware mode routes store/exchange, fetch-add,
fetch-OR and fetch-AND through ``rtos_hal_atomic.hpp`` and the RISCue provider.
These execute AMOSWAP.W, AMOADD.W, AMOOR.W and AMOAND.W with aq=rl=1 and compiler
memory clobbers. The pinned toolchain uses reviewed ``.insn`` encodings because
its assembler does not recognize the named Zaamo ISA subset. No compiler atomic
builtins, LR/SC, AMOCAS or runtime atomic library are introduced.

Load and compare-exchange retain local interrupt masking. Zaamo alone has no
compare-and-swap instruction. Consequently this mixed API remains single-CPU;
other harts or DMA must not modify its storage concurrently. Hardware RMW support
does not make its compare-exchange safe against independent masters. Use only
aligned, capable normal RAM, never arbitrary MMIO. Cross-domain device ordering,
cache maintenance and DMA quiescence remain BSP/driver responsibilities.

Kernel scheduling, wait queues, mutex priority inheritance and ownership recovery
still use their existing synchronization. No busy-wait locks are substituted.
Zaamo-enabled kernel startup checks RISCue's custom feature CSR 0xfc0 bit 0;
applications using hardware atomics before kernel start must check availability
first. A core lacking that CSR may trap rather than provide a software fallback.

Matched test builds
-------------------

``atomic-baseline`` and ``atomic-zaamo`` compile the same
``examples/atomic_stress.cpp`` at ``-O3`` into separate directories. Both use
the same new RISCue source snapshot and native-RAM RTL test wrapper; only the
atomic feature selection differs. This isolates backend correctness from the
old AXI wrapper's different bus timing. Existing overnight/AXI tests continue to
use their older pinned core and remain separate.

.. code-block:: sh

   cmake --preset host -DRTOS_SIMULATION=ON
   cmake --build --preset host --target simulation_tool
   cmake --preset atomic-baseline -DRTOS_PREBUILT_NATIVE_TOOLS=/path/to/fresh/native-tools
   cmake --build --preset atomic-baseline
   cmake --preset atomic-zaamo -DRTOS_PREBUILT_NATIVE_TOOLS=/path/to/fresh/native-tools
   cmake --build --preset atomic-zaamo
   VERILATOR=/opt/verilator-5.048/bin/verilator cmake --build --preset host --target sharp_atomics_baseline
   VERILATOR=/opt/verilator-5.048/bin/verilator cmake --build --preset host --target sharp_atomics_zaamo

Set ``RISCUE_ZAAMO_SOURCE_DIR`` to a source tree containing the optional feature.
On this OptiPlex, ``/home/jeffd/src/nexrt/RISCue-Zaamo`` is a separate RTL snapshot
copied from the uncommitted Zaamo work in ``C:\\dev\\RISCue`` (base commit
68cbd6e). It is not the older published/pinned checkout. Each simulator manifest
hashes the actual RTL inputs, model, firmware and verification/runtime binaries;
retain this snapshot with those artifacts to reproduce the run. The original
development tree is not modified by the RTOS test.

Three real kernel threads compete for 192 increments. Returned old words claim
unique tickets; every ticket bit must be present exactly once. Workers set and
clear independent bits across yields, exchange values with an independent
telescoping-sum check, exercise both compare-exchange branches, sleep across
timer interrupts and retire. Completion requires 384 compare-exchange checks,
all worker exits, five intact stack guards and exact totals. The host verifier
requires timer interrupts and zero observed AMOs in baseline mode, positive
AMO activity in hardware mode. It also audits the final ELF: baseline must
contain zero AMO instructions and hardware mode must contain supported word
AMOs, with no LR/SC or unsupported atomic operations.

Evidence is separate under ``build/sharp-atomics-baseline`` and
``build/sharp-atomics-zaamo``. Status reports and manifests identify the mode.
The host model fixture ``hardware_atomic_hal_model`` checks routing and API
results but is not counted as actual hardware execution. Real RTL execution is
also simulation evidence, not physical FPGA qualification or cross-master
memory-order proof. The matched fixture is a correctness test, not a complete
latency/throughput benchmark.

Recorded matched validation
---------------------------

Runs ``20261021-025621-602`` (baseline) and ``20261021-025627-587`` (Zaamo)
both completed on the real RISCue RTL model with zero firmware failures,
192 unique increments, all six ticket words filled, 384 compare-exchange checks,
five intact guards and 17 timer traps. The baseline recorded zero AMO requests
and zero AMO instructions in its ELF. Hardware mode recorded 968 RAM AMO
requests and 11 supported aqrl word-AMO instructions in its ELF.

.. list-table:: Matched fixture size at -O3, bytes
   :header-rows: 1

   * - Build
     - Code/constants
     - Initialized data
     - BSS
   * - Baseline
     - 8516
     - 52
     - 5968
   * - Zaamo
     - 8576
     - 40
     - 5968

This fixture's code/constants increase is 60 bytes, including feature discovery;
it is not a universal size or performance estimate for all applications. The
separate 4096-byte boot-stack reserve is excluded from the table. Both ELFs pass
the static allocation audit. Host tests pass 33/33, focused ASan/UBSan checks
pass, and invalid host/ARM/missing-capable-RAM configurations are rejected.
The Cortex-M3 kernel also compiles with this feature disabled. Those compile
checks do not constitute an ARM hardware run.

The C# evidence collector retained and verified 58 files (ELFs, status/results,
manifests, disassembly and verification assemblies) in
``requirements/evidence/zaamo-20261021``. The original scheduled overnight test
remains the feature-disabled, older pinned AXI configuration; its prepare-only
launcher check still passes. No full overnight run was started by this work.
