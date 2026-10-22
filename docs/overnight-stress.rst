Overnight randomized target workload
================================================================================

``examples/overnight_stress.cpp`` is a real-kernel RISC-V application. It is
prepared for an overnight run; compiling it does not execute it. A bounded
launcher smoke check is separate from completing a full soak or hardware run.

Four permanent workers take copied jobs from private single-slot queues. A
dispatcher uses target-side xorshift32 to select workers, task types, seeds,
CPU work lengths, sleep intervals and priorities. The generator needs no heap,
host randomness or entropy device. A fixed master seed reproduces job generation
for the same scheduling observations; timing and job ordering can still differ.

Jobs exercise dependent integer arithmetic, stack scratch-buffer processing,
cooperative yielding, timer preemption, timed queue receives and mutex ownership.
Occasional one-tick sleeps while holding the shared counter mutex exercise
priority inheritance. Every job has a sequence number and checked result; a
supervisor checks per-worker ordering, replayed checksums, stack guards, metrics
and progress. Replay uses the same work function and is not an independent CPU
instruction oracle. Each worker and the dispatcher have a 30-target-second
heartbeat deadline. Successful shutdown requires every accepted job to finish,
all four worker stop acknowledgements, actual worker/dispatcher retirement and
an exact shared-counter total.

Threads cannot be created or restarted after NexRT starts. Jobs repeatedly
arrive and finish on fixed workers; actual thread exits happen only at final
shutdown. Four 1,024-byte worker stacks, two 1,536-byte control stacks, the 1,024-byte idle stack,
fixed queues and a separate boot-stack reserve must fit the target. Stack sizes
are initial budgets, not proven bounds. Bounded 128-byte paint observations are
upper estimates and do not prove headroom.

Build without running
---------------------

.. code-block:: sh

   cmake --preset overnight-stress -DRTOS_SOAK_HOURS=12 \
     -DRTOS_SOAK_SEED=1314411092 -DRTOS_SOAK_CLOCK_HZ=100000000
   cmake --build --preset overnight-stress

The overnight preset selects the RTL wrapper's 100 MHz machine-timer frequency.
The launcher checks a retained ELF clock-identity word and rejects a different
setting. For another execution environment use its actual machine-timer frequency.
Hours are target elapsed time, not
host wall time; an RTL simulator can take much longer. The Release preset uses
``-O3`` and enables synchronization, guards, watermarking, metrics and slicing.
It disables networking, logging and DMA. No physical DMA engine is selected,
so this workload makes no DMA-hardware coverage claim. Host-tool reuse may be
configured using a compatible freshly built native-tools directory on this
machine. Building alone never launches the simulator.

Launcher
--------

``NexRT.Sim`` has separate prepare, bounded smoke and full-soak modes. It uses
the unchanged pinned RISCue PLAIN core with the same AXI/RAM wrapper as the UART
demo, plus a simulation-only RAM read probe. It resolves the status address and
size from the actual ELF, reads the target's real status and detects CPU faults,
unexpected traps, firmware failures and loss of timer progress. It does not
generate synthetic worker results. Enable ``RTOS_SIMULATION`` in the host build
and point it at compatible sibling RISCue and SharpTestBench checkouts::

   cmake --preset host -DRTOS_SIMULATION=ON
   cmake --build --preset host --target simulation_tool
   # Build the firmware separately using the commands above.
   cmake --build --preset host --target sharp_prepare_soak
   # Only when a bounded integration execution is wanted:
   cmake --build --preset host --target sharp_smoke
   # The full overnight launch is explicit:
   cmake --build --preset host --target sharp_soak

``VERILATOR`` may select an installed compatible Verilator executable. This
OptiPlex has version 5.048 at ``/opt/verilator-5.048/bin/verilator``; the system
default is older. ``NEXRT_COCOTB_ROOT`` selects the compatible cocotb package.
The isolated pinned dependencies on this machine are in
``/home/jeffd/src/nexrt/RISCue`` and ``/home/jeffd/src/nexrt/SharpTestBench``.
The native bridge must be built for the selected SharpTestBench checkout.

The host model uses ``-std=c++20 -O3``. The launcher also sets Verilator make
variables ``OPT_FAST``, ``OPT_SLOW`` and ``OPT_GLOBAL`` to ``-O3`` so an appended
default ``-Os`` cannot override the requested optimization. Flags are part of
the model fingerprint. An already running process keeps its existing binary;
a changed launcher or model must be prepared separately until that run ends.

Direct invocation accepts four paths (RTOS root, RISCue, SharpTestBench, ELF),
then ``--prepare-soak``, ``--smoke`` or ``--soak``, and an optional host limit in
seconds (1..43200). Smoke defaults to 300 host seconds and requires at least
eight completed jobs, all four workers, sampled dispatches for all six threads
and three timer traps. Soak defaults to
12 host hours; it can stop much earlier if the target completes or fails.
Only one soak-mode launcher may own this checkout at a time.

Artifacts are isolated under ``build/sharp-soak/runs/<UTC timestamp>``.
``current-run.txt`` identifies the most recent attempt; check ``launcher.pid``
and ``simulator.pid`` for actual liveness. ``progress.jsonl`` and atomic
``status.json`` updates are available while the process runs. The manifest
records source, firmware, simulator, managed verification and runtime hashes.
Results distinguish ``completed``, ``smoke_passed``, ``wall_limit_reached`` and
``failed``. A host cutoff without full target completion is explicitly
incomplete even when the verification case returns normally. Launch/build or
outer timeout failures retain ``launcher-failure.txt``. The process supervisor
applies the host bound plus a 30-second cleanup allowance and kills owned
descendants if the simulator does not exit. Per-subprocess preparation retains
the existing 15-minute bound. No physical board programming is performed.

Observe ``nexrt_soak_status`` through the debugger: phase 1 means running,
2 means completed successfully and 3 means failed. It includes completed jobs,
per-mode/per-worker counts, receive timeouts, timer-residency busy percentage,
dispatch counts, sampled thread states and base/effective priorities, stack paint
estimates and the first failure's worker/sequence. States follow ``rtos::State``:
0 unused, 1 ready/running, 2 sleeping, 3 finished, 4 blocked. Samples are taken
every 100 target milliseconds, so they do not show every intervening transition.
Runtime counters are low 32-bit diagnostic views. A running debugger read can
be torn; final phase 2/3 records are stable. Idle residency estimates workload
occupancy, not independent CPU throughput. The launcher preserves these samples
in JSON; there is no graphical dashboard or UART reporter.
If the CPU or timer stops, the supervisor cannot run: an external debugger or
hardware watchdog is needed to detect that kind of silence.

Failure codes
-------------

Codes 1-8 indicate worker/dispatcher queue, mutex or priority failures; 9-12
indicate snapshot/guard failures; 13-15 indicate invalid/duplicate/reordered or
corrupt completions; 16 is a supervisor receive error; 17 is global lack of
completed work; 18 is a final shared-total mismatch; 19 is a missing task mode;
20 is shutdown timeout; 21 is boot registration failure; 22 is a worker or
dispatcher heartbeat timeout; 23 means an accepted job was not collected before
shutdown. All failure paths retain the first recorded code.
