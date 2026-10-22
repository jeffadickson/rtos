USB hardware-in-the-loop test design
========================================

Status: proposed version 1 contract. This document specifies an implementation
plan; the USB manager, device protocol and FPGA checker are not implemented or
hardware-qualified yet. Existing building blocks are the randomized workload,
queued UART driver, binary trace ring and first-failure capture.

Test arrangement
----------------

One C# manager supervises multiple USB-connected boards independently. Each board
runs its own real-time NexRT workload with a recorded seed and finite duration.
USB carries configuration and summaries, rather than commands for every job.
Prefer native USB or a UART-to-USB bridge for control and a separate UART pin for
the channel under test. The FPGA observes that physical UART pin, independently
of the processor's memory-mapped transmit registers::

   C# manager -- USB hub -- board A: NexRT + seeded tests
                        -- board B: NexRT + seeded tests
                        -- FPGA: UART receiver + independent record checker
                                      ^
                                      +--- board UART TX pin

Each device reports a provisioned board UID, USB/bridge identity, processor and
feature profile, firmware SHA-256, protocol/test-schema versions, timer frequency
and checker/bitstream identity. COM numbers and hub positions are connection
locations, not durable identities. Boards lacking a UID require explicit host
inventory binding; report that limitation. Unsupported PMP/atomic/cache features
are skipped explicitly, never reported as passed.

Commercial RISC-V silicon tests NexRT and that HAL implementation. Exercising the
RISCue RTL, ECC or TMR implementation at hardware speed requires RISCue on an
FPGA. An external UART checker can serve either kind of target.

What to report
--------------

Send one compact summary per second by default; enable detailed tracing only
when needed. Suggested report groups are:

* Run identity, seed, uptime, target timer units/frequency, phase and reset reason.
* Jobs submitted/completed, per-thread progress, dispatches, timeout counts and
  independent expected-versus-observed result mismatches.
* Busy/idle residency, maximum observed scheduling/interrupt latency, stack
  guards, bounded stack-use observations and invariant violations. Label
  observed maxima as measurements rather than worst-case guarantees.
* Queue high-water marks, full/retry counts, transport drops and CRC errors.
* First fault: cause, PC, trap value, status, worker, job sequence and the recent
  binary event window. Ordinary RAM retention ends at reset unless a BSP provides
  and validates retained storage.
* UART checker: byte/record counts, per-thread sequence errors, framing/parity
  errors, overrun, timeout, first bad byte/record and expected/observed signatures.
* Hardware ECC correction, uncorrectable error and TMR disagreement counts only
  where hardware exposes them; include the relevant address/replica identifier.

Thread syndromes
----------------

Treat a syndrome as a compact diagnostic vector. Each job has a stable identity
``(worker, job_sequence, seed, operation_kind)`` and a deterministically defined
input/result. Check the result with an independent reference calculation, record
completion identity, and detect missing/duplicate jobs. Compute per-worker result
signatures in job-sequence order; use fixed-size sequence tracking for outstanding
jobs. Never silently evict an unchecked job from that window.

Register-canary tasks keep distinct integer patterns live across preemption and
blocking operations, with checks around saved/restored contexts. Additional
invariants cover mutex ownership, queue capacity, DMA ticket ownership, guards
and bounded worker-progress deadlines. Floating-point tests require a separately
declared supported context profile.

A whole-run trace CRC varies with legal thread interleavings. It is useful for
replay diagnosis but is not a pass/fail oracle. Neither matching checksums nor
heartbeats alone prove correct execution. Preserve expected/observed results and
counts with every signature. Checksum collisions remain possible.

Small wire protocol
-------------------

Version 1 uses a byte stream (USB CDC ACM or UART bridge), little-endian fields,
COBS encoding and a zero-byte frame delimiter. No native C/C++ struct layout is
sent. The current proposal is exactly 512 decoded bytes: a 40-byte header,
468-byte payload region and a four-byte CRC-32C. Unused payload bytes are zero.
COBS plus delimiter needs at most 516 bytes; use 576-byte frame slots where
64-byte allocation alignment is useful. This replaces the earlier unimplemented
236-byte proposal; no compatibility with that draft is asserted. USB transfer
and serial-read boundaries do not define these logical packet boundaries.

.. list-table:: Fixed header, byte offsets
   :header-rows: 1

   * - Offset
     - Bytes
     - Meaning
   * - 0
     - 4
     - ASCII magic ``NXH1``
   * - 4
     - 1
     - Protocol version, 1
   * - 5
     - 1
     - Message type
   * - 6
     - 2
     - Flags, zero in version 1
   * - 8
     - 2
     - Meaningful payload length, at most 468; remainder is zero padding
   * - 10
     - 2
     - Reserved, zero
   * - 12
     - 4
     - Per-direction packet sequence
   * - 16
     - 16
     - Host-assigned session nonce
   * - 32
     - 8
     - Device boot identity

CRC-32C covers all 508 preceding decoded bytes including padding, excludes its own field, and is
stored little-endian before COBS encoding. Use reflected polynomial
``0x82f63b78``, initial state ``0xffffffff`` and final XOR ``0xffffffff``.
The ASCII bytes ``123456789`` must yield ``0xe3069283``. This CRC detects accidental
corruption; it does not authenticate commands or firmware.

Reject bad magic/version, nonzero reserved fields, incorrect decoded length,
oversized payloads and failed CRC before changing test state. On overlong or
malformed frames, discard through the next zero delimiter. Bound receive work
per service call. Partial USB reads and multiple frames in one read are normal.

Proposed types: ``HELLO=1``, ``INFO=2``, ``CONFIG=3``, ``START=4``, ``STOP=5``,
``ACK=6``, ``STATUS=7``, ``THREAD_STATUS=8``, ``FAULT=9``, ``TRACE_CHUNK=10``,
``UART_CHECK=11`` and ``RESULT=12``. Variable reports use a versioned payload
schema with explicit item count and chunk offset; no unbounded TLV/string scan.
Payload field tables and golden encoded frames are the next implementation gate.

An unsolicited HELLO uses a zero session nonce. While idle, the manager binds a
session by sending HELLO with a fresh nonzero 128-bit nonce and the observed boot
identity; the board echoes both in INFO before accepting CONFIG. A bound session
cannot be replaced during an active run. Subsequent commands and responses must
echo the bound identities. A board without reliable boot identity advertises that capability gap
and must re-handshake after reconnect/reset. Reconnect never silently resumes a
previous run. End a session before its 32-bit packet sequence wraps.

CONFIG/START/STOP carry a command ID; ACK echoes it, the status and effective
configuration identity. Allow one command in flight per device; command IDs
increase monotonically without wrapping in a session. Cache the most recent
command bytes and response. Retry that command ID and identical payload to get
the cached outcome, never repeat its side effects. Older uncached command IDs
are rejected, and a reused ID with different contents is an error. A new START
cannot replace an active run. CONFIG validates
feature support and resource limits before START. There is one active test per
device; changing seed/configuration requires another run identity.

The manager drains reports continuously. STATUS is lossy with explicit sequence
gaps/drop counters. FAULT and RESULT stay latched until acknowledged; retries
are paced and bounded per service call. ACK payloads distinguish command results
from acknowledgements of device reports. A fault/result has a stable logical
report ID; ACK identifies that ID plus boot/run/session identity. Retransmission
may use another transport packet sequence without changing the logical report.

Keep reporting out of the measured path
------------------------------------------

Workers update fixed progress/results storage and do no formatted per-operation
printing. The supervisor validates progress and copies a bounded snapshot into
an owned frame slot; a low-priority transport owner sends it without
waiting for host reads. UART DMA or native USB can reduce CPU service work, but
the BSP must demonstrate ownership, interrupt and cache correctness.

Reserve a fault/result slot separately from replaceable periodic status. On
backpressure, coalesce status and increment a drop counter. A disconnected host
cannot block worker execution or the supervisor. Capture the first failure and
freeze its recent-event window locally before trying to transmit it. Reports do
not feed the watchdog; the supervisor feeds it only after all required workers
meet their configured progress deadlines.

A starting storage budget is four 576-byte TX slots, two 576-byte RX/decode
buffers, separate 576-byte retained fault and result slots, 64 sixteen-byte trace
records and two 576-byte command/reply cache slots: 6784 bytes, excluding ownership descriptors,
task stacks and workload state. Queue small frame-ownership descriptors: the
current RTOS queue's default 128-byte interrupt-masked copy bound must not be
increased merely to carry 512-byte reports. Serialize frames outside the mask;
qualify the cost of the bounded source snapshots separately. This
is a design budget; the final linker map must verify actual storage. A maximum
size summary at 1 Hz is at most 516 stream bytes/second per device, before optional
thread-detail, trace, fault and handshake traffic. USB servicing
and interrupt overhead still need measurement even at that low data rate.

Compare reporting-disabled and reporting-enabled runs for throughput, interrupt
residency and latency. A GPIO timing witness/logic analyzer provides an external
measurement. A second core may own reporting only in a separate multi-core test
profile; the initial RTOS test profile remains single-hart.

FPGA UART checker
-----------------

Use an input synchronizer, baud-qualified UART receiver and start/data/stop-bit
validation, followed by record parsing, sequence checking and signature logic.
Specify supported baud, clock error and reset behavior. The checker runs from
its own reference clock; it must observe actual transmitted pin data. An AXI
write monitor is an additional bus-level check, not a substitute for pin checks.

First support the three-thread greeting demo: parse complete lines, independently
check each worker's round/result, and permit arbitrary ordering between workers.
Then add a deterministic binary test record with worker ID, sequence, payload
length, seeded PRBS payload and record CRC. The FPGA regenerates each record's
expected bytes, detects first mismatch, and verifies each worker's sequence.
Serialize each record through the queued UART driver; interleaved fragments must
fail parsing/CRC. This mode tests both the driver and the physical UART path.

The capture bank contains observed/expected byte counts, record counts, CRCs,
sequence gaps/duplicates, first mismatch index, framing/parity/overrun counters
and timeout/stuck-line indicators. Require the expected count and completion
marker as well as matching signatures; no output is not a passing empty CRC.
Use separate control and test channels initially. A shared channel would require
the checker to decode the protocol and select explicit test-data messages.

ARM/FREEZE/READ/CLEAR commands for the checker are session-bound. Snapshot all
multiword counters coherently with a request/acknowledge clock-domain crossing,
then read a frozen bank. Preserve first mismatch until the host archives it;
clear only for an explicitly armed new capture. Partial/faulted captures remain
failed or indeterminate and never become passing results.

Manager and delivery sequence
-----------------------------

The manager should be a C# CLI first, with one bounded asynchronous session per
device and isolated evidence directories. Keep raw frames, decoded JSONL,
firmware/checker hashes, requested/effective configuration, seed and timing.
Classify passed, test-failed, target-reset, unresponsive, transport-lost,
stop-timeout, host-cutoff and evidence-incomplete separately. Silence or USB loss cannot establish the target fault's
cause and cannot produce a pass. A host deadline does not prove target duration.

Implement in stages:

#. Freeze payload layouts and shared C++/C# golden vectors; test fragmented,
   corrupt, duplicate, reordered, oversized and stale-session input.
#. Add single-board binary telemetry using fixed storage and the existing stress
   supervisor; measure its overhead before enabling many boards.
#. Add multi-board manager runs, hotplug/reset detection, reproducible seed plans
   and bounded fault/result retrieval.
#. Simulate the FPGA UART checker with known-good and injected bit, framing,
   missing-byte, duplicate-record and stuck-line faults. Check that each intended
   fault is detected, then validate it with physical loopback hardware.
#. Run the RISCue-on-FPGA matrix with independent timer, UART and hardware syndrome
   evidence. Board selection, pin voltages, clocks and USB bridge are BSP inputs.

References
----------

See :doc:`hil-verification-plan` and ``requirements/hil-design.csv`` for the
specialist review, prioritized tests and planned host/monitor/equipment requirements.

* `COBS original paper <https://www.stuartcheshire.org/papers/COBSforSIGCOMM/>`_.
* `AMD AXI UARTLite PG142 <https://docs.amd.com/v/u/en-US/pg142-axi-uartlite>`_.
  The current NexRT simulation endpoint implements a TX subset, not the full IP.
* `Raspberry Pi microcontroller interfaces <https://www.raspberrypi.com/documentation/microcontrollers/microcontroller-chips.html>`_.
  Actual USB/UART/DMA integration remains board-specific.
