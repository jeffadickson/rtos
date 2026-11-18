QEMU networking and task tracing evidence
========================================

Source baseline04bb4a33b9583e6e5634b03d365f10af8b0a6099; feature/qemu-network.
Verification date2026-11-18. No physical board programmed and no CI dispatched.
Raw artifacts: E:/Artifacts/NexRT/qemu-network-20261118-114443. See retained
source hashes and tool-specific logs; this is finite engineering evidence,
not platform qualification or a worst-case latency bound.

Implemented slice
-----------------

* Actual NexRT RV32 kernel on QEMU8.2.2 virt, one hart, M-mode,10MHz CLINT.
* Fixed modern virtio queues, static lwIP UDP, host loopback forwarding.
* Static optional scheduler/mutex ring, versioned CRC-32C/COBS partial-write UART.
* Native .NET10 Avalonia replay/TCP task timeline with priority inheritance,
  owner connections, bounded history, and unknown intervals across data loss.
* NexRT-owned static littlefs adapter; upstream engine remains unmodified.
* Separate Boost1.83 host evaluation; no Boost target dependency.

Execution and checks
--------------------

The native CSharp host client repeatedly passed129 actual host UDP echoes
through QEMU forwarding: zero, one,511,512 and seeded random payloads, then
513-byte rejection and valid recovery. Seed0x4e585254. Its owned QEMU process
was observed stopped after each finite run. Trace and no-trace images both
passed the no-allocator/no-dynamic-initializer audit. The final enabled image
uses text40604,data182,bss61232 bytes; the no-trace image uses
text34748,data42,bss42528 bytes. These are different application variants:
the trace profile additionally includes three priority-inheritance witness
threads/stacks, so the size difference is not recorder-only overhead.

Host MMIO modeling exercises66000 received completions through16-bit wrap,
TX copied ownership/busy/reclaim, invalid features, malformed queue completions,
RX insufficient destination capacity and unsupported offloads. Seed0x514e4554.
Trace tests independently decode COBS, check CRC/fields over10000 reproducible
records(seed0x4e585431), check overflow/loss and exercise partial UART writes.
Clang ASan/UBSan standalone virtio/monitor runs passed. UDP libFuzzer with
ASan/UBSan completed10000 runs(seed741923,max_len1536), with no reported defect.
No process RAM cap was imposed. Five isolated mutation controls were killed:
mount state, NOR overwrite acceptance, UDP destination, trace CRC and TX copy.
These controls are not a comprehensive mutation score.

Littlefs retains64 operation-boundary reset cuts and adds32 failure/recovery
cuts plus callback reentry negatives. Missing LFS_NO_MALLOC deliberately fails
compilation; the upstream allocator fallback returns null. Restored media must
be unmounted/remounted after I/O errors; immediate same-mount retry is not
promised. The guard supplies no concurrent-call synchronization.

The real2044-record QEMU capture includes one inherited-priority event and one
mutex wait, no sequence gaps and one rejected ASCII startup prefix. Timer
metadata reports10MHz. Native GUI full and zoom screenshots were visually
inspected. See tools/NexRT.TaskView/verification.txt for33 test results,
source hashes, GUI coverage and failed/historical mutation campaign distinctions.
Missing dispatch, loss, rejected frame and restart regressions stop continuous
state spans and retain unknown markers. Final GUI mutation score is unmeasured.

Reproduction
------------

From an existing checkout and existing artifact parent:

.. code-block:: sh

   cmake --build build/host --target network_test_tool services_tool configure_tool -j8
   cmake --build --preset qemu-udp --target demo -j8
   build/host/native-tools/NexRT.NetTest/NexRT.NetTest smoke build/qemu-udp/demo.elf OUTPUT 10007
   build/host/native-tools/NexRT.NetTest/NexRT.NetTest self-test
   ctest --test-dir build/services-coverage -j4 --output-on-failure
   build/host/native-tools/NexRT.Services/NexRT.Services mutations . EXISTING_OUTPUT
   build/host/native-tools/NexRT.Services/NexRT.Services coverage build/services-coverage OUTPUT
   build/host/native-tools/NexRT.Requirements/NexRT.Requirements check .

See docs/qemu-network.rst and docs/task-view.rst for configure/GUI commands and
wire layout. StrictDoc and Sphinx are real third-party tooling, not emulations.
Release C++ uses C++20,-O3,enabled loop/SLP vectorization; verified SIMD is not
claimed. Linux and Windows build outputs remain separate.

Open qualification and integration work
---------------------------------------

Physical UART/DMA/cache behavior, serial USB reconnect, target execution cost
and sustained UART throughput have not been measured. There is no automatic
priority-inversion verdict or calibrated wall-clock CPU utilization. The viewer
uses raw timer counts and can show an incomplete capture. Dedicated board
monitor UART and physical COM input remain integration work; QEMU TCP is
implemented. GUI Native AOT/Linux execution and MCP command exposure remain
unimplemented qualification/integration gaps. Boot registration is still sealed
at start; the monitor does not create/delete threads dynamically. No formal
proof or Dafny-generated component was selected for this C++/native GUI slice.
Requirements remain PartialSourceUnverified; finite tests do not close all
hardware or concurrency requirements.

Final recorded results
----------------------

The complete native host configuration passed65/65 CTest entries (zero failures),
including network-client malformed inputs and real UDP no-response timeout.
The independent Windows viewer rerun passed33/33 with the real capture loaded.
GCC gcov scopes: monitor ring/codec16/16 lines,25/30 branch outcomes;
monitor stream9/9 lines,18/22 outcomes; virtio70/77 lines,91/136 outcomes;
static littlefs port36/36 lines,68/90 outcomes. Branch counts are compiler
outcomes, include exception paths, and are not MC/DC. GUI Core100% lines,
98.24% branches; GUI application90.78% lines,62.24% branches. Kernel target hooks,
assembly and Native AOT host client are not included in those coverage scopes.
No coverage percentage is claimed for physical hardware.

Final trace-enabled ELF SHA256:
4a71a8d3075d2de817a2b4967664dc5ee41ee5bc414d9cd22455f4753fd018ee.
Final-source smoke:129 echoes passed,0.675s client test interval; QEMU PID1907470
was observed stopped. That timing is one finite test interval, not a benchmark
or target execution speed claim. Linker still warns that flat M-mode LOAD
segments have RWX permissions; no memory-isolation claim is made.

Review disposition: Release assertion stripping in the new monitor fixture was
fixed; documentation now describes the actual shared16550 startup framing;
missing-data intervals were corrected with9 regressions; mutex owner connectors
and arrowheads are present. A pre-existing timestamp-only ELF fixture directory
collision under parallel CTest was fixed with atomic-create bounded suffix
retries. No unknown user changes were discarded.

Real StrictDoc0.30.4 HTML export passed for275 requirements. Sphinx warning-as-error HTML build passed after correcting heading underlines. Final review confirmed the missing-data and owner-connector findings addressed.
