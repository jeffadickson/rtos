C# tooling and XDoc
===================

Project scripts and automation use C# with XML documentation comments. The
policy is recorded in AGENTS.md. Sphinx is the third-party documentation-only
Python exception, with its minimal docs/conf.py configuration.

The .NET 10 projects are ``tools/Rtos.Tools``, ``tools/NexRT.Requirements``,
``tools/NexRT.Configure``, ``tools/NexRT.Workbench``, ``tools/NexRT.ArmSim``
and ``tools/NexRT.Sim``, plus the collector ``tools/NexRT.Evidence``.
CMake publishes native executables by default and
emits XDoc XML beside each executable. Compiler, AOT and linker warnings are
errors. The .NET SDK is needed to build the tools; the native executables run
without the .NET runtime. SDK and platform prerequisites must be installed.

Commands
--------

.. code-block:: sh

   cmake --preset host
   cmake --build --preset host --target native_tools
   build/host/native-tools/Rtos.Tools/Rtos.Tools self-test
   build/host/native-tools/Rtos.Tools/Rtos.Tools audit --nm /path/to/nm --elf build/riscue/demo.elf
   build/host/native-tools/NexRT.Configure/NexRT.Configure list .
   build/host/native-tools/NexRT.Configure/NexRT.Configure cmake-test .
   build/host/native-tools/NexRT.Requirements/NexRT.Requirements check .
   cmake --build --preset riscue --target static_contract_check

Windows executable names add ``.exe``. CTest, ELF audits, static-contract
fixtures and requirements checks invoke the selected executable directly.
The audit rejects known allocator and dynamic-initialization symbols. The
static-contract command compiles positive and negative firmware fixtures for
constant initialization, constructor rejection and allocator rejection.
The self-test checks symbol boundaries, paths with spaces and option errors.
Processes use argument lists without shell interpolation, with both output
streams drained. Firmware audit/compiler invocations and simulation runners
share bounded capture; see :doc:`host-memory` and :doc:`arm-stress`.

The configurator generates deterministic CMake initial caches from
``config/system.json``. Its tests check capacities, topology, readiness and
feature output. The native ``cmake-test`` command also invokes the actual CMake
configuration guards in script mode, covering syntax, boundaries, alignment and
feature dependencies. CTest runs both configuration commands. No firmware is
compiled by these guard checks. See :doc:`platforms`. The requirements tracker validates
traceability and generates views; see :doc:`requirements-process`.

Optional C++ analyzers
----------------------

Install the native Cppcheck and clang-tidy executables on the build host, then
reconfigure CMake so it can discover them. On Ubuntu these are the ``cppcheck``
and ``clang-tidy`` packages. No Python analyzer runner is used. For custom
locations, set ``NXR_CPPCHECK_EXECUTABLE`` and ``NXR_CLANG_TIDY_EXECUTABLE`` to
absolute executable paths at configure time.

.. code-block:: sh

   cmake --preset host
   cmake --build --preset host --target cppcheck
   cmake --build --preset host --target clang_tidy
   # Run both:
   cmake --build --preset host --target cpp_analysis

These targets are opt-in and do not build or change firmware. Each prints its
tool version, then reads the active build's ``compile_commands.json`` for
include paths, definitions and language options. Use Ninja or Makefiles; other
generators and missing executables cause an explicit failure when requested.
Ordinary builds do not require either analyzer. Retain the console output and
tool versions for review; the targets do not apply fixes or create suppressions.

Cppcheck enables errors, warnings, performance and portability diagnostics;
reported findings fail the target with exit code 1. clang-tidy uses the checked-in
``.clang-tidy`` correctness profile, including core/C++/Unix path analysis,
selected bug-prone checks, null-pointer modernization and override checks.
All emitted warnings are errors. Project headers are checked through configured
C++ translation units, including host test fixtures. Disabled features and
sources absent from that build are outside its coverage: run each relevant
configuration rather than treating one host profile as the complete kernel.

The same targets are available in firmware presets. For RV32, clang-tidy is
given ``--target=riscv32-unknown-elf`` and Cppcheck uses its ``unix32`` integer
and pointer model. The QEMU Cortex-M3 preset uses ``--target=arm-none-eabi``
and the same Cppcheck 32-bit model. Firmware definitions come from that preset, including the
selected application. Cppcheck's platform model does not emulate the RISCue CPU.
Standalone assembly is excluded from clang-tidy's source list; neither analyzer
qualifies context switching, interrupt interleavings, MMIO, latency or stack
capacity. The linked ELF allocation audit remains a separate required check.

Start with the checked-in profile and review each finding. Any future inline
Cppcheck suppression or clang-tidy ``NOLINT`` needs a narrow scope and documented
reason; do not disable a diagnostic globally just to clear an existing finding.
The initial integration uses Cppcheck 2.13.0 and clang-tidy 18.1.3;
release qualification still needs pinned tool versions and retained evidence.

The initial scoped exceptions are recorded in
``config/cppcheck-suppressions.txt`` and adjacent source comments. Host fixtures
deliberately call operations inside assertions, which their CMake targets force
on; memory fixtures deliberately exercise zero-length memset and byte conversion.
The Ethernet descriptor is deliberately copied. Cppcheck 2.13 short-literal
bounds reports in web/logging code ignore its terminating-NUL exits. Re-review these
exceptions when their code/contracts or analyzer versions change.

Native AOT execution
--------------------

``RTOS_NATIVE_TOOLS=ON`` is the default. The ``native_tools`` target publishes
all four tools under ``build/<preset>/native-tools/<assembly>/<assembly>``.
Each individual ``rtos_tools``, ``requirements_tool``, ``configure_tool`` and ``workbench_tool``
target also builds its native executable. Output and intermediates are isolated
between build directories, so firmware presets can build independently.
With ``RTOS_ARM_SIMULATION=ON``, ``arm_sim_tool`` adds the native
``NexRT.ArmSim`` QEMU runner; see :doc:`arm-stress`.

The runtime identifier is selected from the build host OS and CPU, even during
firmware cross-compilation. This Linux x64 host produces Linux x64 tools, not
RISC-V, ARM or MicroBlaze firmware executables. Windows needs matching Visual
C++/Windows SDK Native AOT prerequisites; Ubuntu needs Clang and zlib development
files. Publishing may restore the matching .NET AOT packs. Prerequisite failure
is explicit; there is no automatic managed fallback.

``-DRTOS_NATIVE_TOOLS=OFF`` explicitly selects managed compatibility on an
unsupported host. That mode builds DLLs under ``managed-tools`` and requires
the .NET runtime to execute them. It is separate from the default native path.

JSON metadata is generated at compile time; reflection-based JSON is disabled.
Checkpoint evidence uses concrete schema types rather than anonymous objects.
Native host tools may use normal host-managed memory. They do not run on the
RTOS CPU, and firmware static-storage/no-heap rules remain unchanged. Use a
fresh build directory when changing host toolchain or runtime environment.

See Microsoft's `Native AOT prerequisites <https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/>`_
and `JSON source generation <https://learn.microsoft.com/en-us/dotnet/standard/serialization/system-text-json/source-generation>`_.
