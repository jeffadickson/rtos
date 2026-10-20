Host memory analysis
================================================================================

Two optional presets add execution-time memory checks without adding bytes to
firmware. Cppcheck and clang-tidy remain separate static analysis targets.

``host-asan`` selects Clang and enables AddressSanitizer and
UndefinedBehaviorSanitizer for native C++ code, with frame pointers and
nonrecovering findings. Install the matching compiler runtime, then run:

.. code-block:: sh

   cmake --preset host-asan -DRTOS_PREBUILT_NATIVE_TOOLS=/path/to/native-tools
   cmake --build --preset host-asan
   ctest --preset host-asan

``host-valgrind`` creates Memcheck variants of native C++ fixtures, including
all fixed random seeds. These fail with exit code 99 on memory errors or
definite, indirect or possible leaks. Reachable runtime bookkeeping is reported
but is not classified as a lost allocation. Each check has a 120-second limit.
Install Valgrind or specify its path; a user-local extracted installation can
also set its support directory:

.. code-block:: sh

   cmake --preset host-valgrind -DRTOS_PREBUILT_NATIVE_TOOLS=/path/to/native-tools \
     -DNXR_VALGRIND_EXECUTABLE=/path/to/valgrind -DNXR_VALGRIND_LIB=/path/to/libexec/valgrind
   cmake --build --preset host-valgrind
   ctest --preset host-valgrind

These presets use separate directories and cannot be enabled together.
They reject firmware/cross-compilation configurations. C# tools and external
simulators are not C++-instrumented by these options.

Host checks cover the configured C++ fixtures and models. They do not execute
the target's allocator policy or prove absence of every possible leak. Firmware
still uses boot-static storage and its linker/symbol audits. Those audits now
require defined symbols: stripped or empty listings fail closed. The known-name
allocator check cannot detect arbitrary renamed/custom allocators by itself.
Perform the audit before stripping a delivery image, and retain the unstripped
audited ELF and its identity with evidence.

The memory presets also build a deliberately invalid host-only probe. Native
C# gates require a nonzero exit and the specific heap-use-after-free ASan,
signed-overflow UBSan, or invalid-read Memcheck diagnostic. A generic crash or
tool startup failure does not count. These expected diagnostic logs are named
``negative-asan.log``, ``negative-ubsan.log`` and ``negative-valgrind.log`` in
their respective build directories. The invalid probe is absent from ordinary
host/firmware builds and is not part of a passing application workload.

Compiler/linker contract fixtures include stripped-image rejection alongside
constant initialization, constructor/preinitializer/finalizer rejection and
reachable allocator detection. Audit/compiler subprocesses use the shared
120-second execution-and-drain bound and eight-million-character output cap.
