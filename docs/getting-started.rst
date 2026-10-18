Building and first application
==============================

Prerequisites
-------------

Use CMake 3.22 or newer, Ninja, the .NET 10 SDK, and a native C++17 compiler for
host tests. Firmware requires a RISC-V bare-metal GNU toolchain with RV32
ILP32 multilib support. The firmware requires no C++ standard library.
Sphinx 8.2.3 requires Python 3.11 or newer.

Host validation
---------------

From the source directory::

   cmake --preset host
   cmake --build --preset host
   ctest --preset host

Repeat with the ``minimal`` preset to exercise disabled metrics, guards,
round-robin, and Ethernet. Host tests verify portable logic; they do not execute
the RISC-V context-switch port.

Cross compilation
-----------------

With a toolchain on PATH::

   cmake --preset riscue
   cmake --build --preset riscue

Or configure a full toolchain prefix::

   cmake --preset riscue -DRISCV_TOOLCHAIN_PREFIX=/path/to/bin/riscv64-unknown-elf-
   cmake --build --preset riscue

Changing compiler or ABI requires a fresh build directory. The result is
``build/riscue/demo.elf`` and its map. An automatic post-link audit rejects
known allocator symbols and dynamic C++ initialization symbols. Review the
map and stack-usage files for your exact configuration.

For the current Windows/WSL development machine, run in Ubuntu-24.04 and source
``/mnt/c/dev/Enter-RISCue.sh`` first. The existing toolchain prefix is
``/mnt/c/dev/RISCue/.tools/riscv/root/usr/bin/riscv64-unknown-elf-``.
No changes to the RISCue RTL checkout are required.

Application
-----------

.. literalinclude:: ../examples/blink.cpp
   :language: cpp

Thread control objects, stacks, arguments, and diagnostic name strings must
remain valid for the full kernel lifetime. Check every registration result.
At boot configure each peripheral, establish ownership, register every thread,
then call ``start`` with the actual core clock frequency. ``start`` never returns.
The example publishes ``rtos_thread_info`` and ``rtos_system_info`` for debugger
inspection; the slow thread refreshes both snapshots every 100 logical ticks.

Use only constant-initialized globals: this minimal startup does not execute
``.init_array`` constructors. A static ``Thread``, ``Stack<N>``, or ``Ethernet``
object with its default member initializers satisfies this restriction. Do not
use application global objects requiring runtime constructors or thread-local
C++ runtime support.

Documentation build
-------------------

Create a project-local environment and install the pinned documentation tool::

   python3 -m venv build/docs-venv
   build/docs-venv/bin/python -m pip install -r docs/requirements.txt
   build/docs-venv/bin/python -m sphinx -n -W --keep-going -b html docs build/manual/html

On Windows use ``build/docs-venv/Scripts/python.exe``. When Sphinx is installed
in the project-local environment or on PATH, reconfigure CMake; then
``cmake --build --preset host --target docs``
does the same strict HTML build in the preset's output directory.
Sphinx can also generate LaTeX sources using ``-b latex``; compiling those to PDF
requires a separately installed TeX distribution. HTML is the validated delivery
format for this baseline.

Legacy Makefile
---------------

The root Makefile is a convenience wrapper around CMake presets. CMake is the
canonical configuration and build implementation; do not maintain separate
compiler flags or feature definitions in application build scripts.
