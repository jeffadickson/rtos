Build your first minimal application
================================================================================

This example uses one statically registered thread plus private idle. It increments
a debugger-visible counter and sleeps for one tick. It needs no UART, allocator,
C library, network stack or synchronization module. The existing ``nano`` preset
selects it; no additional project or generated application source is needed.

Prerequisites and board inputs
--------------------------------------------------------------------------------

Run from the repository root in a Linux/WSL shell with CMake 3.22+, Ninja, the
.NET 10 SDK/Native AOT prerequisites, and bare-metal GCC supporting RV32IM/Zicsr
ILP32. A ``riscv64-unknown-elf-`` toolchain works if it contains the RV32 multilib.
Native C# tools execute on the host, never on the FPGA.

Before running, replace ``machine_timer_hz`` with the actual ``mtime`` frequency.
The 10 MHz constant is an example, not a detected board clock. Confirm the timer
addresses and RAM map in :doc:`configuration` and :doc:`assembly-port`. Programming
the image requires your board's verified probe/loader and FPGA bitstream; the
repository does not guess a flashing command.

.. _libgloss-overview:

Where libgloss fits
--------------------------------------------------------------------------------

**libgloss is the hardware-facing support layer commonly used with newlib**,
an embedded C library. Newlib implements familiar C functions; libgloss supplies
board or execution-environment support underneath them. Depending on the target,
that support can include startup code (often called ``crt0``), linker scripts,
and low-level system-call hooks. These hooks need not be CPU trap instructions:
on bare metal they can be ordinary functions that call a driver. See the upstream
`libgloss guide <https://sourceware.org/newlib/libgloss.html>`_.

A typical newlib console path is::

   application printf -> newlib stdio -> _write hook -> board UART driver

Other integration hooks can include ``_read``, ``_close``, ``_fstat``,
``_isatty``, ``_lseek`` and ``_exit``. ``_sbrk`` is commonly used to supply heap
growth to the allocator. The precise names and reentrant wrappers depend on
the newlib port. The `newlib manual <https://sourceware.org/newlib/libc.html#Syscalls>`_
describes this interface.

This RTOS currently supplies its own small runtime boundary:

.. list-table:: Current firmware responsibilities
   :header-rows: 1
   :widths: 28 72

   * - Responsibility
     - Implementation here
   * - Reset/startup
     - ``ports/riscue/start.S`` establishes registers and the boot stack,
       clears BSS and calls ``main``. The loader supplies initialized data.
   * - Memory layout
     - ``ports/riscue/link.ld`` places sections and reserves boot-stack space.
   * - Basic memory operations
     - ``src/memory.cpp`` provides ``memcpy``, ``memmove``, ``memset`` and ``memcmp``.
   * - Compiler arithmetic helpers
     - Explicitly linked ``libgcc``; this is distinct from newlib and libgloss.
   * - Diagnostic output
     - ``rtos::Logger`` formats bounded records; a caller-provided sink drains
       them to a UART. See :doc:`uart-demo` for the AXI example.

The firmware link uses ``-nostdlib`` and explicitly selects ``libgcc``. It does
not link newlib/libgloss, install their syscall hooks, or provide ordinary libc
``printf``. GCC's `link-options reference <https://gcc.gnu.org/onlinedocs/gcc/Link-Options.html>`_
explains why compiler helper routines may still be needed with ``-nostdlib``.
Our current output path is::

   worker -> Logger::printf -> static record queue -> drain callback -> AXI UART

There is no libgloss dependency to install for this starter application. Keeping
this boundary explicit helps preserve the small, boot-static configuration.

If a future application needs newlib compatibility, add it as a separately
budgeted integration: retain a single startup/linker owner, route console hooks
through an owned driver, and define unsupported operations. Newlib also needs
appropriate per-thread reentrancy state and locking; supplying ``_write`` alone
does not establish thread-safe stdio. Review its
`reentrancy contract <https://sourceware.org/newlib/libc.html#Reentrancy>`_.
Keep all permitted storage static, check code/data/stack costs, and preserve the
allocation audit. A stub that rejects heap growth does not by itself make an
allocation-dependent library path usable. No such compatibility layer is
implemented or enabled by the current presets.

Complete application
--------------------------------------------------------------------------------

.. literalinclude:: ../examples/nano.cpp
   :language: cpp
   :linenos:
   :caption: examples/nano.cpp -- selected by the nano preset

The control block and stack stay alive forever. Priority 1 is sufficient here.
At the default 1000 ticks/second, one tick ends at the next boundary; this is not
a precise one-millisecond execution period. Qualify stack margin on your target.

Configure and build
--------------------------------------------------------------------------------

Build native tools once, then reuse their validated source/host fingerprints.
Substitute your real compiler prefix, including its trailing hyphen.

.. code-block:: sh

   cmake --preset host
   cmake --build --preset host --target native_tools
   cmake --preset nano \
     -DRISCV_TOOLCHAIN_PREFIX=/path/to/toolchain/bin/riscv64-unknown-elf- \
     -DRTOS_PREBUILT_NATIVE_TOOLS=build/host/native-tools
   cmake --build --preset nano

The build automatically audits static-only allocation. It produces
``build/nano/demo.elf`` and ``build/nano/demo.map``: the application and kernel
are linked together. Build success establishes compilation/linking and allocation
checks, not board execution. The flat RAM port currently emits an RWX load-segment
warning. After editing the source, repeat ``cmake --build --preset nano``.

Nano explicitly disables synchronization, logging, Ethernet, HTTP, storage,
metrics, guards and time slicing. An include does not enable a service. Progress
to the ``sync`` or ``logging`` preset for the patterns in :doc:`simple-start`.

Inspect memory and execution
--------------------------------------------------------------------------------

.. code-block:: sh

   /path/to/toolchain/bin/riscv64-unknown-elf-size build/nano/demo.elf
   /path/to/toolchain/bin/riscv64-unknown-elf-objdump -d build/nano/demo.elf
   build/host/native-tools/NexRT.Workbench/NexRT.Workbench footprint . build/nano/demo.elf

``size`` excludes the separate boot reserve (4096 bytes by default). RISCue executes code from
RAM, so code consumes RAM too. The native footprint command includes section gaps
and boot reserve and refreshes ``docs/footprint.rst`` and ``site/footprint.json``.
Current measured bytes are in :doc:`footprint`.

On a verified board/debugger setup, break at ``_start``, ``main`` and
``rtos_entry`` and inspect ``nano_iterations`` for progress. The counter is not
printf output or a thread synchronization primitive. Confirm timer delivery,
saved-register preservation and stack margin before deployment.
