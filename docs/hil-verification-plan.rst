OS hardware-in-the-loop verification plan
=========================================

This proposal synthesizes three specialist agent reviews: RTOS correctness,
hardware/FPGA test equipment, and the host executive/embedded monitor. The reviews
inspected the existing kernel and IPC contracts. All 60 requirements in
:download:`the HIL design baseline <../requirements/hil-design.csv>` are Planned;
this is not implementation or target qualification evidence. The requirement IDs
are a separate design namespace linked to the existing NXR baseline.

Decisions from the review
--------------------------------------------------------------------------------

Use three cooperating components: a C# host manager, a small on-target test
executive, and an independent hardware observer. The target executes autonomous
scenarios. The host selects scenarios and collects evidence. The observer measures
physical timing/UART/reset behavior without depending on the tested scheduler.

Use fixed 512-byte decoded reports with COBS framing. Queue small frame ownership
descriptors, keeping the existing 128-byte queue copy limit. The proposal's
packet layout and storage budget are in :doc:`hardware-in-loop`.

The on-target monitor should activate and quiesce boot-created workers. NexRT
seals registration at ``start()`` and ``exit()`` permanently finishes a worker.
It currently has no arbitrary suspend/resume, thread restart/delete, or queue
reset/delete API. Peripheral external-interrupt dispatch also needs BSP work;
host ISR-context tests do not establish a working hardware UART/DMA ISR path.

What matters first
------------------

.. list-table:: Prioritized acceptance tests
   :header-rows: 1
   :widths: 22 39 39

   * - Property
     - How to exercise it
     - Required evidence
   * - Context integrity
     - Assembly register canaries under CPU load, timer preemption and blocking
     - Zero mismatches across the declared switch count; capture first bad register
   * - Scheduling
     - Controlled high/medium/low priorities, donation chains and equal-priority peers
     - Correct effective-priority selection and declared round-robin behavior
   * - Deadlines and interrupts
     - External stimuli, GPIO entry/resume witnesses, delayed ticks and wrap fixtures
     - No early logical wake; bounded observed latency with capture uncertainty
   * - IPC correctness
     - Full/empty queues, direct handoffs, waiters, timeouts and completion races
     - Unique message/token conservation; no unexplained loss or duplicate grant
   * - Resource ownership
     - Repeated runs, cancelled jobs, abandoned mutex owner and DMA abort faults
     - Settled ownership or explicit quarantine; no stale waiter or reused live buffer
   * - Memory integrity
     - Guards, bounded watermark observations and static-allocation audit
     - No unexpected guard failure or allocation; scan completeness is reported
   * - UART thread safety
     - Concurrent seeded records from three or more workers into the queued driver
     - Independently checked pin data, per-worker sequences, counts and terminal marker
   * - Liveness and recovery
     - Stalled worker, USB reader stopped, link unplugged, reset during work
     - Correct failure classification, first-fault preservation and isolation of other boards

A sleep deadline is the time a thread becomes ready, not a promise that it resumes
at that time when a higher-priority thread remains runnable. Similarly, a low
priority thread intentionally starved by a strict-priority profile is not itself
a scheduler failure. Each test declares which workers must make progress.

Measure interrupt arrival, handler entry and worker dispatch separately. USB
timestamps cannot measure those target latencies. Calibrate the observer's clock,
sampling resolution, synchronizer uncertainty and GPIO-witness overhead. Set
per-board acceptance limits before arming; an observed maximum is not WCET proof.

Monitor commands and state
--------------------------------------------------------------------------------

The initial command set should expose INFO/LIST_TESTS, CONFIGURE, ARM, START_RUN,
REQUEST_STOP, STATUS, FETCH_FAULT, FETCH_TRACE, FETCH_RESULT and REARM. These are
logical operations; their versioned payload tables must be frozen before a codec
is implemented. RESET is separate and follows the declared bench reset policy.
Expose stable numeric test/worker/queue IDs, never arbitrary addresses or writes
to kernel-owned thread fields.

.. list-table:: Target state transitions
   :header-rows: 1

   * - State
     - Allowed action and transition
   * - IDLE
     - CONFIGURE establishes a fresh run ID/generation after checking idle state and session
   * - CONFIGURED
     - ARM verifies resources, selects workers and prepares the independently armed observer
   * - ARMED
     - START_RUN releases the start gate; RUNNING is reported after all selected workers acknowledge
   * - RUNNING
     - REQUEST_STOP or the configured run goal enters QUIESCING
   * - QUIESCING
     - Settled workers/resources produce COMPLETED; an expired grace produces FAULTED/STOP_TIMEOUT
   * - COMPLETED
     - Retain RESULT; successful REARM returns IDLE after evidence acknowledgement and test-resource quiescence
   * - FAULTED
     - Retain first fault; quiescence is a separate fact and is not implied by this state
   * - REBOOT_REQUIRED
     - No reuse; reset the declared ownership domain and perform a fresh handshake

A detected fault transitions to FAULTED from any active phase. Continue only
bounded evidence capture and safe quiescence work. REARM is forbidden while any
test worker, workload mutex/waiter, test peripheral transfer or quarantine remains
unresolved. Successful REARM acknowledges archived terminal evidence, invalidates
prior run handles and returns IDLE. Permanent monitor command-wait queues and
workers parked at their idle control barrier are expected; they are not unresolved
workload ownership. Never include control queues in a workload drain operation.
Sacrificial owner-death workers that call ``exit()`` require a reset before that
worker pool is reused. Reset alone is insufficient if an external DMA engine can
continue accessing memory: the BSP/fixture must establish its quiescence too.

ACK for START means accepted, not all workers running. ACK for STOP means stop
requested, not quiescent. Report barrier completion and terminal RESULT separately.
COMPLETED is a lifecycle state; the verdict can be passed, failed or incomplete.
Manual early stop cannot pass a longer duration/operation goal.

CONFIGURE validates boot/session identity plus expected IDLE state and establishes
the new run identity. START/STOP/REARM and object operations then validate the
current run and object generation. Retry semantics must not restart work, clear
faults or drain another run's queue.

Thread and queue control
------------------------

Permanent workers wait for a typed command, run a bounded phase, settle ownership
and return to their command-wait loop. They do not call ``exit()`` for ordinary
completion. Place cooperative stop checks between bounded work units and use
finite IPC waits or another verified wake path. If a worker does not cooperate,
report STOP_TIMEOUT and preserve its occupied state; do not forcibly suspend a
lock owner or unwind a live stack.

For queues, prefer predefined producer/consumer scenarios and snapshots first.
Optional injection commands must specify the registered queue's message schema,
run generation and job ID. An accepted send proves admission, not completion.
Do not reconstruct, memset or clear queue internals. A drain operation is allowed
only after participating producers/consumers and waiters are quiescent; use normal
receive calls with bounded work and explicitly account discarded messages.

Give the command executor/supervisor a declared scheduling budget and reservation
above ordinary test-worker priorities. Keep transport service lower priority and
never block control on a test-owned lock. Deliberate interrupt-masking, excessive
priority or scheduler-corruption scenarios may make the monitor unreachable;
their manifest must require external watchdog/reset supervision instead of
promising cooperative STOP. A serial heartbeat does not establish worker health.

What a 512-byte snapshot provides
--------------------------------------------------------------------------------

.. list-table:: Proposed STATUS layout
   :header-rows: 1

   * - Offset
     - Bytes
     - Contents
   * - 0
     - 40
     - Version/type, payload length, packet sequence, session nonce and boot identity
   * - 40
     - 64
     - Run identity, target time, phase, overall progress, reporting drops and validity
   * - 104
     - 256
     - Eight 32-byte thread summaries
   * - 360
     - 64
     - UART/checker counts, error summary and signatures
   * - 424
     - 64
     - First-failure summary; larger context is retrieved separately
   * - 488
     - 20
     - Zero padding
   * - 508
     - 4
     - CRC-32C over bytes 0..507

The fixed thread slots are a page, not a limit on configured thread count. Use
thread IDs, total count, page information and sample epochs. Serialize outside
interrupt-masked sections and qualify the bounded source snapshot cost. Different
observer clock domains or sample epochs must not be presented as one instantaneous
atomic system snapshot. Unsupported measurements have explicit invalid flags;
zero is a potentially valid measurement and must not mean unsupported.

Control commands use the same fixed packet size with smaller meaningful payloads
and zero padding. Detailed fault/trace retrieval uses bounded fragments with
stable record ID, total length, offset and integrity. FAULT and RESULT each have
independent retained storage and logical report IDs that survive retransmission.

If the control link is a 115200-baud 8N1 UART bridge, a maximum 516-byte frame
takes about 44.8 ms on the wire; a request followed by its ACK needs about 89.6 ms
before USB/software delay. Native USB may be faster, but is still asynchronous.
Consequently the host controls phases and the target/FPGA generates precise
stimuli locally. Target stop grace begins when the valid stop command is accepted;
the host stop deadline must additionally budget transport and response time.

Independent equipment and fault sensitivity
--------------------------------------------------------------------------------

Start with one board, a control USB connection, a separate UART under test and
an independent timing observer. A logic analyzer can establish the first UART
and GPIO checks; an FPGA or second microcontroller can later generate stimuli,
measure latencies, verify seeded payloads and latch the first mismatch in real
time. Board-specific wiring must state voltage, direction, polarity and safe state.

The FPGA receives the actual pin, regenerates expected per-worker payloads and
checks sequences/counts/terminal markers as well as CRC. Global message ordering
may vary legally; per-worker sequence and required partial-order relationships
are the relevant oracle. A run seed reproduces inputs, not oscillator phase,
interrupt alignment or USB arrival timing. Record phase markers and recent events.

Before trusting the checker, inject bad data with a freshly correct CRC, bit and
framing errors, missing/extra bytes, duplicated/reordered records, incomplete final
markers, stuck lines, observer overflow, counter rollover and reset. Each required
detector must produce its declared non-pass result while known-good streams pass.
This verifies semantics and detector sensitivity rather than CRC wiring alone.

Host executive and evidence
---------------------------

Use a C# CLI with a board/fixture inventory and one independent bounded async
session per target. Its manifest must include firmware/checker hashes, feature
profile, seed plan, required operation counts, target duration, timing limits,
monitor-overhead budget, stop grace, watchdog response and allowed reset policy.
Device discovery must not use COM numbering as durable identity. Keep a required
versus optional capability distinction and explicit skipped tests.

Classify PASSED, TEST_FAILED, TARGET_RESET, TRANSPORT_LOST, UNRESPONSIVE,
STOP_TIMEOUT, HOST_CUTOFF and EVIDENCE_INCOMPLETE separately. Transport loss does
not establish whether the kernel failed. A pass requires all configured goals,
target assertions and required external captures with complete identity/evidence.
Expected-fault scenarios declare their allowed faults before arming and use a
separate fixture/reset policy so their effects cannot contaminate later tests.

Archive raw frames, decoded JSONL, command chronology, target/host times, fault
identity, trace completeness, linker/static-allocation audit and calibration.
Fill report queues, stop host reads, unplug one target and flood another as
acceptance tests. Healthy targets must continue and retain their evidence.

Recommended delivery order
--------------------------

#. Freeze and test fixed-packet schemas and a bounded monitor state machine.
#. Run context/scheduler/IPC scenarios on one real board with minimal reporting.
#. Qualify reporting interference and independent GPIO/UART observations.
#. Add the multi-board C# executive and reset/hotplug/fault retrieval cases.
#. Add the FPGA semantic UART checker and prove fault-detection sensitivity.
#. Run the RISCue FPGA profile matrix and physical DMA/ECC/TMR tests where supported.

As a reference pattern, `Zephyr's device-testing hardware maps
<https://docs.zephyrproject.org/latest/develop/test/twister.html>`_ match scenarios
to boards and fixtures. NexRT's proposed executive remains C# and uses its own
explicit hardware inventory and evidence contracts.
