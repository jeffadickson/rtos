Moving development to another computer
======================================

``NexRT.Handoff`` is a C#/.NET 10 tool with XML API documentation. CMake publishes
a native executable by default; managed execution is an explicit fallback.
``HANDOFF-START-HERE.txt`` and ``OPTIPLEX-RESTART-PROMPT.txt`` describe recovery
and the current development state. No commit is required to preserve local work.

Archive and recovery
--------------------

The pack command saves reachable Git refs/history in bundles, an index patch,
tracked deletions and all nonignored tracked/untracked worktree files. External
RISCue and SharpTestBench checkouts are separate snapshots. SHA-256 and byte
counts cover every payload in ``MANIFEST.tsv``; a sidecar hashes the ZIP itself.
Verify that sidecar after copying, then extract into a fresh directory::

   chmod +x tools/linux-x64/NexRT.Handoff
   tools/linux-x64/NexRT.Handoff verify .
   tools/linux-x64/NexRT.Handoff restore . "$HOME/src/nexrt-optiplex"

Restore requires a nonexistent destination and refuses worktree paths targeting
Git internals or escaping repository roots. It verifies hashes before restoring
and compares every copied source file. It creates sibling repositories and
restores staged changes before overlaying worktree changes. A failed restore
may leave an incomplete destination; retry into another fresh directory.
``audit-zip FILE`` performs extraction and a full restoration in temporary
storage, checking HEAD, index patches, worktree hashes and deletions.

Ignored build outputs, SDK installations, compiler caches, virtual environments,
Git configuration/hooks, credentials and reflog-only objects are excluded.
HTTP origin credentials/query strings are stripped. This is a source handoff,
not a full computer backup. Existing Linux x64 simulation libraries can be
included; they require compatible host libraries and must be rebuilt on an
incompatible ABI. Historical evidence remains historical after the move.
Worktree bytes are preserved exactly. Git newline settings belong to the new
machine; different settings can report additional text changes even when the
restored bytes match. Match the source machine's Git newline policy before
interpreting such differences. No global Git configuration is changed.

Dependencies before a build
----------------------------------------

Run the extracted native doctor before configuring a new build::

   tools/linux-x64/NexRT.Handoff doctor /path/to/restored/rtos host
   tools/linux-x64/NexRT.Handoff doctor /path/to/restored/rtos all

Profiles may be comma-separated. ``host`` checks Git, CMake >= 3.22, Ninja,
.NET 10 SDK, the C++ compiler and, on Linux, Native AOT clang/zlib linking and
execution. ``docs`` checks Sphinx (project pin 8.2.3); ``analysis`` checks
Cppcheck and clang-tidy; ``arm`` checks cross tools and QEMU; ``riscv`` checks
cross tools; ``rtl`` additionally checks Linux, Verilator >= 5.048, Make,
RISCue RTL, SharpTestBench, cocotb native libraries and .NET hosting files.
These prerequisites do not establish target correctness or qualification.
Memory tooling remains separately configured by the host sanitizer/Valgrind
presets described in :doc:`host-memory`.

Overrides are literal argument pairs:

================= ===========================
Option            Environment equivalent
================= ===========================
--arm-prefix      ARM_TOOLCHAIN_PREFIX
--riscv-prefix    RISCV_TOOLCHAIN_PREFIX
--qemu            QEMU_ARM
--cppcheck        CPPCHECK
--clang-tidy      CLANG_TIDY
--cxx             CXX
--verilator       VERILATOR
--riscue          RISCUE_SOURCE_DIR
--sharp           SHARPTESTBENCH_SOURCE_DIR
--cocotb          NEXRT_COCOTB_ROOT
================= ===========================

``--sphinx`` selects sphinx-build and ``--report`` writes a text report.
``DOTNET_ROOT`` overrides automatic .NET discovery. Cross prefixes must include
the trailing hyphen. A nonzero exit reports missing/old/nonworking dependencies;
the tool never installs software. Correct the listed problems and retry.
The source-machine dependency report in an archive is informational only.

Native Linux x64 tooling is preferred on Ubuntu 24.04/WSL. If incompatible,
``dotnet tools/managed/NexRT.Handoff.dll`` accepts the same arguments with an
installed .NET 10 runtime. Linux/WSL is required for the RTL runner.

Fresh build and documentation
----------------------------------------

From the restored rtos checkout::

   cmake --preset host
   cmake --build --preset host --target check_dependencies
   cmake --build --preset host
   ctest --preset host

``check_dependencies`` checks host tools plus the selected firmware/RTL profile;
``check_all_dependencies`` explicitly checks every optional profile. Both work
with Ninja or Make generators. Configuration itself needs its basic tools;
the standalone packaged doctor diagnoses these before CMake can run.
Dependencies are not added implicitly to firmware or optional-module builds.
Ninja publishes one C# host tool at a time by default to limit Native AOT
compiler memory use. ``RTOS_HOST_TOOL_JOBS`` can increase that limit on a
machine with enough RAM; C++ compilation remains independently parallel.

For DMA, reconfigure with ``-DRTOS_DMA=ON -DRTOS_SYNC=ON``. Build fresh native
tools; do not set ``RTOS_PREBUILT_NATIVE_TOOLS`` to the previous machine's cache.
Sphinx is the permitted third-party Python runtime exception::

   python3 -m venv build/docs-venv
   build/docs-venv/bin/pip install -r docs/requirements.txt
   cmake --preset host
   cmake --build --preset host --target docs

New custom automation remains C# with XDoc. The normal docs build is strict
Sphinx and checks generated requirements exports. Compiler/QEMU/analyzer paths
are CMake cache options documented in the respective target chapters. Simulation
defaults to sibling RISCue/SharpTestBench directories and a sibling
``runtime/cocotb`` directory; explicit environment/cache overrides remain usable.
