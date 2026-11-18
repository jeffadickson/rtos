Static services verification record
===================================

Scope and source identity
-------------------------

Executed November 18, 2026 on WSL Ubuntu-24.04, starting at
``d71bc1d9c4c832f8cf5c2bccef189db7f6c4711b``. This record describes finite host
and RV32 instruction-model evidence, not physical-board qualification. The
accompanying ``inputs.sha256`` identifies tested source bytes; the commit
containing this record provides the complete source and dependency manifest.

Bulk logs, coverage JSON, fuzz corpus, dependency downloads and reports are under
``E:\Artifacts\NexRT\static-services-20261118-094531`` on the execution machine.
Linux build outputs remain on the native WSL filesystem. The real StrictDoc
HTML export is staged on that filesystem because exporting many small files
directly to the exFAT artifact drive was substantially slower; a compressed
copy is archived with the bulk artifacts. Required bounded summaries are here.

Toolchain
---------

GNU C/C++ 13.3.0 host; GNU riscv64-unknown-elf C/C++ 13.2.0 target;
.NET SDK 10.0.402 with Native AOT packs 10.0.14; StrictDoc 0.30.4;
littlefs 2.11.3; lwIP 2.2.1; ETL 20.40.0. Clang/LLVM libFuzzer and ASan/UBSan
versions are retained in ``tool-versions.txt``. Dependency revisions and license
notices are retained alongside this record. No library is qualified merely by
appearing in that manifest.

Executed acceptance
-------------------

* Default C++20 host regression: 56 tests passed, zero failed.
* Combined optional-services profile: 60 tests passed, zero failed, including
  instruction-model completion, cutoff and wrong-clock controls.
* Dedicated GNU coverage selection: eight tests passed, zero failed.
* Clang ASan/UBSan littlefs and UDP tests: two passed, zero failed.
* Storage-only and UDP-only configurations: their respective service tests pass.
  All-off, fully disconnected configuration builds and passes its memory test.
  The native source-graph checker passes all three configurations, excluding
  disabled libraries and TCP/DNS/DHCP/socket/IPv6 source files.
* ETL: four aligned typed-pool loans, explicit exhaustion guard, reuse and return
  to empty pass with exceptions/RTTI disabled. ETL remains evaluation-only.
* littlefs: blank-media mount rejection, explicit format, write/read/remount,
  invalid inputs/states, injected I/O failure and 64 operation-boundary reset
  fixtures pass. The reset fixture checks one small inline file replacement.
* UDP: 300 reproducible payload cases, including zero and 512 bytes, valid
  nonzero request checksums, bad IPv4/UDP checksums, wrong ports, a 513-byte
  rejection, busy TX, both fixed-pool exhaustion paths and recovery pass.
  A further 1000 bounded malformed frames use seed ``0x4e585254``.
* LLVM libFuzzer: 5000 inputs, seed ``1314411092``, maximum length 1536, no
  sanitizer failure. The retained corpus began with ARP and evolved across
  bounded runs. This finite campaign emphasizes Ethernet/ARP parsing; it is not
  comprehensive UDP/IP conformance or exhaustive fuzzing.
* Three deliberate isolated-copy mutations are killed: mount-state publication,
  NOR overwrite checking and UDP reply destination. The unmodified mutation
  baseline passes. Three targeted controls are not a general mutation score.

Target execution and removal
----------------------------

The existing RV32 interpreter executes ``build/services-firmware/demo.elf`` with
262144 bytes of modeled RAM. Its reported status is
``[2,0,1,32,32,34]``: completed phase, zero firmware failure, one persisted file,
32 checked UDP echoes, 32 peer-worker activations and 34 service dispatches.
There are 33 timer interrupts and 66 synchronous scheduling exceptions. This
does not represent a physical MAC, a host UDP socket or cycle-accurate RTL.

The optimized combined ELF has 67652 text bytes, 16422 data bytes and 40720 BSS
bytes, according to GNU size: 124794 section bytes, excluding the separately
reserved 4096-byte boot stack and any layout padding. It passes the post-link
no-general-allocator/no-dynamic-initializer audit. The ELF retains the documented
machine-mode RWX load segment; no new isolation claim is made.

The nano profile has 3576 text, 24 data and 1136 BSS bytes, 4736 section bytes.
Its ELF passes the same audit and contains no littlefs, lwIP, ETL or new
separator-span symbols. The original 32512-byte RAM extent remains its default.
The normal RISCue blink image also builds under C++20 and passes the audit.
These are current profile measurements, not a controlled before/after benchmark.

Coverage
--------

GCC/gcov Debug instrumentation, with actual executed counts:

.. list-table:: Selected project-source coverage
   :header-rows: 1

   * - Source
     - Lines hit / total
     - Branch occurrences hit / total
   * - src/storage.cpp
     - 27 / 27 (100%)
     - 53 / 72 (73.6%)
   * - src/udp.cpp
     - 50 / 50 (100%)
     - 53 / 92 (57.6%)
   * - include/rtos_littlefs.hpp
     - 21 / 21 (100%)
     - 42 / 50 (84.0%)
   * - src/memory.cpp
     - 37 / 39 (94.9%)
     - 32 / 32 (100%)
   * - sim/interpreter/main.cpp
     - 58 / 74 (78.4%)
     - 200 / 560 (35.7%)

Lines are merged by source line; branches sum the arcs emitted per translation
unit/template instance. Header percentages therefore have a different aggregation
scope. Adapter source files are listed individually; upstream, test and standard
library results are separate in the raw TSV. Compiler exception/short-circuit
arcs are not waived. Missing branch coverage includes rare initialization and
transport-error paths. The two unhit memory-wrapper lines execute in target
simulation but target code itself is not instrumented by gcov.

Native AOT C# helpers and target kernel/third-party target execution are not
assigned coverage percentages here. The C# helper commands and negative checks
ran, but Coverlet was not run on a separate managed build. Coverage of this
selected host scope is neither whole-project coverage nor proof of correctness.

Review findings and dispositions
--------------------------------

A read-only fresh-agent review inspected the adapters, pools, tests, simulation,
ETL evaluation and helper tools. This was an AI review, not external human experts.

* Accepted: request UDP checksums had been zero. A payload-corruption negative
  test first failed; the fixture now computes nonzero checksums, and rejection
  plus subsequent valid recovery pass.
* Accepted: header branch aggregation differs from line aggregation. Scope is
  now explicit in the helper API comment and documentation.
* Internal regression: old configuration tests expected storage to fail as
  unimplemented. They now check supported storage and invalid combinations.
* Internal regression: importing upstream Filelists created unused protocol
  targets. An effective-source check failed on TCP, then passed after removing
  that import and retaining only the intended source set.
* Internal regression: minimal typed profiles did not clear cached new options.
  A negative check failed before the renderer began explicitly clearing lwIP
  and ETL evaluation; dependency validation is covered too.
* C++20 sample migration: deprecated volatile increments failed with warnings
  treated as errors. Explicit read/write updates preserve their prior single-
  owner observation semantics; they are not advertised as atomic operations.
* The SDK-selected Native AOT pack failed NU1903. Native helpers now use patched
  10.0.14, and fingerprints include the shared property file. No audit warning
  was suppressed.

Reproduction
------------

From an existing checked repository directory, the Sphinx ``static-services``
page documents presets, offline dependency settings and target invocation.
Principal commands used were:

.. code-block:: sh

   cmake --build build/host -j4
   ctest --test-dir build/host --output-on-failure
   cmake --build --preset host-static-services -j4
   ctest --test-dir build/static-services --output-on-failure
   cmake --build --preset services-rv32-sim --target demo -j4
   build/static-services/sim/interpreter/nexrt-rv --elf build/services-firmware/demo.elf --ram-bytes 262144 --host-seconds 60 --max-instructions 200000000
   build/host/native-tools/NexRT.Services/NexRT.Services configuration build/services-off
   build/host/native-tools/NexRT.Services/NexRT.Services configuration build/services-storage-only
   build/host/native-tools/NexRT.Services/NexRT.Services configuration build/services-udp-only
   build/host/native-tools/NexRT.Services/NexRT.Services coverage build/services-coverage OUTPUT
   build/host/native-tools/NexRT.Services/NexRT.Services mutations . OUTPUT
   cmake --build build/host --target docs strictdoc

Verify OUTPUT's parent exists before selecting a new evidence directory.
The retained configure logs specify dependency overrides, compilers, Debug
coverage flags, sanitizer/fuzzer options and exact paths. Release compilation
uses C++20, -O3 and enabled loop/SLP vectorization; no verified SIMD claim is made.
No hardware was programmed, no twelve-hour soak launched and no CI enabled.
