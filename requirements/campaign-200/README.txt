200-iteration requirements acceptance campaign

Completed 200 distinct authored acceptance scenarios, numbered 1..200, in 20
milestones. Each scenario is one acceptance-focused engineering iteration;
milestones contain ten scenarios and require a real build plus cumulative CTest.
The iterations.tsv ledger links requirements, scope, result and test-source hash.
Checkpoint directories retain actual build/test logs and each milestone's parent
commit. All milestones were committed locally. No push was performed.

The final milestone suite passed 55/55 CTest entries. All 200 new scenarios run
inside twenty of those entries. The sanitizer profile passed 57/57 entries.
The 71 independent RISCue PMP firmware fixtures passed. Zaamo firmware completed
with 968 AMOs, expected tickets/CAS/guards and no failure. A real RISC-V firmware
probe printed 'campaign helpers PASS' and passed the static no-heap/no-dynamic-
initializer ELF audit. The three-worker UART RTL simulation passed again using
the changed kernel, with complete pin-decoded messages from every worker.
Strict Sphinx documentation passed.

Production changes include a fixed-state periodic release calculator, bounded
binary trace, coherent first-failure capture, typed MMIO policies, UART argument
validation, deterministic initial scheduler tie ordering, ELF segment/entry
validation and global diagnostic-symbol integrity. Checkpoints 16, 19 and 20
retain failing regression evidence before their production fixes.

The requirements tracker moved 18 stale/planned entries to source-backed partial
implementation: 96 planned entries at campaign start, 78 at completion. None was
promoted to target-qualified Verified by these host tests. Outstanding clauses
include absolute kernel sleep, physical BSP/DMA/cache/ISR timing, reset-retained
fault handling, NMI safety and hardware fault qualification. ARM compiler/QEMU
are absent on this WSL host, so ARM firmware execution was not rerun here.

New optional helpers reserve storage only when instantiated. Trace/fault helpers
require the existing synchronization provider. Typed MMIO requires a valid BSP
device mapping and ordering policy; it does not supply platform fault recovery.
The original overnight run was not restarted or modified by this campaign.

Reverification:
 cmake -S . -B build/host -G Ninja -DRTOS_INTERPRETER=ON
 cmake --build build/host -j2
 ctest --test-dir build/host --output-on-failure
 build/host/campaign_16 --case 153

See docs/requirements-campaign.rst for scope, source/API notes and limitations.
