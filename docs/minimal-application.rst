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

``size`` excludes the separate 4096-byte boot reserve. RISCue executes code from
RAM, so code consumes RAM too. The native footprint command includes section gaps
and boot reserve and refreshes ``docs/footprint.rst`` and ``site/footprint.json``.
Current measured bytes are in :doc:`footprint`.

On a verified board/debugger setup, break at ``_start``, ``main`` and
``rtos_entry`` and inspect ``nano_iterations`` for progress. The counter is not
printf output or a thread synchronization primitive. Confirm timer delivery,
saved-register preservation and stack margin before deployment.
