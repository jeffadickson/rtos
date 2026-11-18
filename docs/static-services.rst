Optional static storage and UDP services
============================================================

Approved scope and design
-------------------------

Move the project and analysis commands to C++20. Keep littlefs and lwIP as
independent opt-in libraries, with an independent ETL evaluation target. Default
builds must not fetch or link these dependencies. Pin upstream revisions and
preserve their licenses. Use the existing single-owner Ethernet adapter and
boot-static storage rather than replacing the scheduler or IPC implementation.

The littlefs service supplies fixed volume and per-file caches, an explicit
flash callback boundary, and a NOR simulator that enforces erase/program rules.
Only one owning worker calls a volume. Failed mount never automatically formats
media. File handles come from a fixed compile-time inventory.

The network service uses lwIP NO_SYS=1, one owner, fixed packet/control-block
pools, IPv4/ARP/ICMP/UDP only, and a bounded UDP echo payload. Its test endpoint
injects real Ethernet/IP/UDP packets and independently checks outgoing bytes.
NO_SYS describes stack ownership; it does not disable NexRT scheduling. No
sockets, TCP, DHCP, DNS, hardware MAC or physical flash qualification is claimed.

The target demonstration runs the actual NexRT kernel and both libraries on the
existing RV32 instruction model. A separately named larger-RAM simulation profile
may be used; the 32 KiB hardware profile remains unchanged. No physical device is
programmed. QEMU is acceptable but adding another CPU port is unnecessary when
the existing interpreter can execute the same firmware.

Implementation plan
-------------------

1. Add failing functional tests for NOR programming, littlefs persistence and
   failures, pool exhaustion, and wire-level UDP echo. Record the failing build.
2. Change C++20 end to end; add independent CMake options and pinned upstream
   dependencies. Implement the storage and network adapters against those tests.
3. Evaluate ETL typed-pool alignment, exhaustion, reuse and constructor behavior;
   record whether it should replace the generation-tagged NexRT pool.
4. Run host regression, malformed-input and seeded randomized checks; collect
   line and branch coverage and mutation evidence for the new adapters.
5. Run a boot-static firmware demonstration under the RV32 instruction model;
   audit the linked ELF for allocators and dynamic initializers. Verify stripped
   configurations and report feature sizes and simulation boundaries.
6. Update stable NXR requirements, export with real StrictDoc, build Sphinx,
   review the final changes, commit and push the feature branch.

Review focus
------------

Test partial writes and sync failure before asserting persistence; distinguish
filesystem errors from formatting authorization. Exercise double open and fixed
file-slot exhaustion. Never retain adapter RX memory in lwIP. Check pbuf-chain
copies, oversized UDP payloads and output backpressure. Bad packet checksums
must not trigger echo. Pool allocation is dynamic ownership within fixed
capacity, not permission to introduce a general heap. Constructor and allocator
audits apply to the complete linked target, including third-party code.

Progress and evidence
---------------------

Implementation and measured results are recorded here as checks complete.
No hardware qualification follows from host or instruction-model execution.

Use and configuration
---------------------

``RTOS_STORAGE``, ``RTOS_LWIP`` and ``RTOS_ETL_EVALUATION`` default OFF. lwIP
requires ``RTOS_ETHERNET``; none requires a kernel heap. The host evaluation
and complete target demonstration have presets:

.. code-block:: sh

   # From an existing, verified repository root:
   cmake --preset host-static-services
   cmake --build --preset host-static-services --target littlefs_service udp_service etl_evaluation nexrt-rv
   ctest --test-dir build/static-services -R 'littlefs_service|udp_service|etl_evaluation' --output-on-failure
   cmake --preset services-rv32-sim
   cmake --build --preset services-rv32-sim --target demo
   build/static-services/sim/interpreter/nexrt-rv --elf build/services-firmware/demo.elf --ram-bytes 262144 --host-seconds 60 --max-instructions 200000000

Check that the checkout directory exists before entering it. In PowerShell:

.. code-block:: powershell

   $repo = Join-Path (Get-Location) 'rtos'
   if (-not (Test-Path -LiteralPath $repo -PathType Container)) { throw 'Missing NexRT checkout' }
   Set-Location -LiteralPath $repo

Configure on Linux with the existing RISC-V toolchain for target execution.
Third-party sources can be supplied offline using
``FETCHCONTENT_SOURCE_DIR_LITTLEFS``, ``FETCHCONTENT_SOURCE_DIR_LWIP`` and
``FETCHCONTENT_SOURCE_DIR_ETL``. Enabled dependencies otherwise resolve immutable
Git commits. Disabled features do not fetch those repositories. Exact revisions
and license notices are under ``requirements/static-services/dependencies.json``.

Freestanding C sources require C library headers. The tested compiler lacked
them; the build used headers extracted from Ubuntu's pinned ``libnewlib-dev``
package, not a Newlib runtime archive. Supply ``RTOS_C_LIBRARY_INCLUDE`` pointing
at the header directory when needed. The local fallback is
``build/newlib-headers/usr/include/newlib``. Two separator-span routines join
the existing freestanding memory primitives; host tests call prefixed entry
points so libc cannot accidentally satisfy those test calls.

Storage contract
----------------

``rtos::storage::Volume`` has one internal file-operation slot. It owns two
64-byte volume caches, one 64-byte file cache and a 16-byte lookahead buffer,
in addition to littlefs metadata. ``write`` opens, truncates/writes and closes;
``read`` opens, reads up to capacity and closes, returning the byte count or a
negative error. Neither promises POSIX descriptors or atomic file replacement
for arbitrary sizes. ``last_error`` preserves the littlefs error. One worker
owns the volume; other workers should send it bounded requests.

The NOR fixture reserves 32 blocks of 512 bytes in the target demonstration.
``erase_all`` explicitly initializes a fixture; it is never called because a
mount failed. Tests cover 64 operation-boundary reset injections for a small
inline replacement. This finite test does not qualify physical torn writes,
controller caches, flash wear or arbitrary power-loss timing.

Network contract
----------------

The single lwIP instance belongs to one owner until reset. It uses IPv4 address
192.0.2.2/24 and UDP port 7 by default. The service polls frames and services
timeouts using the caller's monotonic milliseconds. Do not call raw lwIP APIs
from other workers or ISRs. There is no stop/rebind API; a failed initialization
after stack ownership is claimed requires reset.

Four RX packet blocks and four 1600-byte allocation-pool blocks are fixed at
build time; control blocks and timeout records also use fixed pools. These loans
do not constitute a growing general heap. The service copies borrowed RX frames
and flattens TX pbuf chains before the synchronous MAC callback. The echo ceiling
is 512 bytes, including valid empty datagrams. A full TX path is a reported
error; the echo service does not retry.

The wire probe feeds Ethernet frames through real lwIP input and checks response
bytes, ports, IPv4 and UDP checksums. Requests have nonzero UDP checksums and
negative tests verify rejection. This simulated MAC is not an externally
reachable host UDP socket or a qualified physical controller. TCP, DHCP, DNS,
sockets, IPv6 and IP fragmentation are deliberately excluded.

ETL assessment
--------------

ETL 20.40.0 passed a four-slot typed-pool evaluation with 32-byte aligned packet
objects, exhaustion guarding and slot reuse, with exceptions and RTTI disabled.
Its pool requires construction; ordinary pointer loans do not supply NexRT's
generation-tagged stale-handle semantics. A globally constructed pool must not
bypass the firmware constructor audit. Explicit boot construction would need a
documented lifetime protocol.

Retain the current NexRT pool: no measured benefit justifies replacement in
this change. ETL remains an optional host evaluation, with no firmware dependency.
littlefs and lwIP use their own bounded storage.

Verification tools
------------------

Set ``RTOS_SERVICE_COVERAGE=ON`` in a separate GNU Debug build for gcov data.
The native ``NexRT.Services coverage BUILD OUTPUT`` command reports actual
source-line unions and branch-occurrence counts. Header branches are summed
across translation units/template instances; their scope differs from merged
line coverage. Adapter results are separate from upstream libraries, tests and
standard headers. Coverage does not prove correctness.

``RTOS_HOST_SANITIZERS=ON`` supplies ASan/UBSan. Native Clang with
``RTOS_SERVICE_FUZZ=ON`` builds ``udp_fuzz`` against LLVM libFuzzer; retain its
seed, corpus and run count. ``NexRT.Services mutations ROOT OUTPUT`` uses an
isolated copy for three explicit fault controls, not a comprehensive mutation
score. Native helper packs use patched .NET 10.0.14 without changing SDK/TFM
selection; their fingerprint includes ``Directory.Build.props``.

Sphinx remains the engineering manual. StrictDoc 0.30.4 is a separate pinned
environment; the ``strictdoc`` target parses and exports the requirements.
Redirect output with ``RTOS_STRICTDOC_OUTPUT``. The C# exporter alone is not that
validation. Detailed commands, results, coverage and limitations are recorded
in :download:`the verification record <../requirements/static-services/evidence.rst>`.
This campaign does not claim
physical-board, Windows-native or ARM execution.
