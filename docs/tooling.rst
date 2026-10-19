C# tooling and XDoc
===================

Project scripts and automation use C# with XML documentation comments. The
policy is recorded in AGENTS.md. Sphinx is the third-party documentation-only
Python exception, with its minimal docs/conf.py configuration.

The .NET 10 projects are ``tools/Rtos.Tools``, ``tools/NexRT.Requirements``,
``tools/NexRT.Configure`` and ``tools/NexRT.Workbench``. CMake publishes native executables by default and
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
streams drained.

The configurator generates deterministic CMake initial caches from
``config/system.json``. Its tests check capacities, topology, readiness and
feature output. The native ``cmake-test`` command also invokes the actual CMake
configuration guards in script mode, covering syntax, boundaries, alignment and
feature dependencies. CTest runs both configuration commands. No firmware is
compiled by these guard checks. See :doc:`platforms`. The requirements tracker validates
traceability and generates views; see :doc:`requirements-process`.

Native AOT execution
--------------------

``RTOS_NATIVE_TOOLS=ON`` is the default. The ``native_tools`` target publishes
all four tools under ``build/<preset>/native-tools/<assembly>/<assembly>``.
Each individual ``rtos_tools``, ``requirements_tool``, ``configure_tool`` and ``workbench_tool``
target also builds its native executable. Output and intermediates are isolated
between build directories, so firmware presets can build independently.

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
