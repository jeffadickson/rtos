Three-thread RISCue UART demonstration
======================================

Integration
-----------

This local simulation runs the actual C++ kernel and firmware instructions
on the existing RISCue pipelined core in PLAIN mode, with RF_TMR and fault
injection disabled. The upstream AXI master bridge retains its own protection
logic. The RTOS repository supplies a simulation wrapper with ordinary 32 KiB
RAM and a TX-only AXI4-Lite UART endpoint. Neither external checkout is modified.
This is simulation evidence, not FPGA programming or hardware timing validation.

PLAIN's internal memory interface transports raw low-32-bit words; the upstream
AXI master bridge expects SECDED codewords. The integration explicitly encodes
requests for that bridge and decodes its read responses back to raw PLAIN words.
Uncorrectable bridge response decoding is a core fault. The protected generic
``riscue_axi_core`` wrapper cannot simply be selected as PLAIN without this
adaptation. This representation conversion does not add ECC to ordinary RAM.

The wrapper uses reset PC zero and the existing internal machine timer registers.
RAM occupies ``0x00000000..0x00007fff``; the firmware linker continues to keep
its image/boot stack below ``0x7f00``. UART TX is ``0x10000004``; status at
``0x10000008`` reports bit 3 when full and bit 2 when idle. These two offsets
resemble AXI UARTLite, but this endpoint implements no RX, control register,
interrupts or transmit FIFO and is not a complete UARTLite implementation.
Unmapped accesses and writes while full return SLVERR. AW and W are captured
independently; responses persist until accepted; RAM honors byte strobes.

The simulated clock is 100 MHz; the machine timer increments every clock and
the firmware requests 1000 ticks/second. UART uses eight clocks per bit
(12.5 Mbaud) to shorten simulation. Select a board-appropriate clock and baud
before hardware use; no achieved FPGA frequency is inferred.

Worker behavior
---------------

Three boot-registered workers have equal priority 2 and individual 1536-byte
stacks. Idle has a separate 512-byte stack. Round robin is enabled. Each worker
retains a distinct 32-bit arithmetic state and publishes its ID, round number,
result and started-worker mask. The firmware loops forever. Worker three also
drains the copied-record logger through a nonblocking, single-owner UART sink;
there is no fourth console worker. A full record queue causes a retry with yield.
The normal loop sleeps after publishing; the drain owner finishes queued output
before sleeping. Queue and pending-record storage are fixed at boot.

Before its first print, each worker performs 30000 arithmetic iterations with
no yield, sleep or IPC call. The first printed started mask must already be 7.
With the selected tick period this demonstrates timer preemption during CPU
work, rather than relying on the later cooperative queue/retry calls.

The C# SharpTestBench case decodes start/data/stop bits from the UART pin,
rejects malformed/interleaved records, independently calculates each arithmetic
result, checks consecutive per-thread rounds and requires at least three complete
records from every worker and machine-timer trap activity. The simulator then
ends while the firmware's intended loop remains infinite. It does not merely
watch AXI register writes or print synthetic host-side thread messages.

Build and run
-------------

Use the existing Linux/WSL checkouts and installed SharpTestBench native host
and pinned cocotb GPI libraries. Custom orchestration and verification are C#;
no Python runner is invoked. The managed test DLL is required by SharpTestBench's
reflection-based discovery; the orchestration tool has a native AOT target.

From the RTOS root in Ubuntu-24.04::

   cmake --preset host -DRTOS_SIMULATION=ON
   cmake --build --preset host --target native_tools
   cmake --preset uart \
     -DRISCV_TOOLCHAIN_PREFIX=/mnt/c/dev/RISCue/.tools/riscv/root/usr/bin/riscv64-unknown-elf- \
     -DRTOS_PREBUILT_NATIVE_TOOLS=build/host/native-tools
   cmake --build --preset uart
   cmake --build --preset host --target sharp_demo

``RISCUE_SOURCE_DIR``, ``SHARPTESTBENCH_SOURCE_DIR`` and ``RTOS_SIM_ELF`` are
host CMake cache settings. The defaults use ``/mnt/c/dev/RISCue``,
``/mnt/c/dev/SharpTestBench`` and ``build/uart/demo.elf``. The runner accepts
those four paths directly too. ``NEXRT_COCOTB_ROOT`` overrides the installed
cocotb package directory, ``DOTNET_ROOT`` selects .NET, and
``RISCV_TOOLCHAIN_PREFIX`` selects objcopy. Prerequisites are .NET 10.0.402,
Native AOT dependencies, the RISC-V compiler, Verilator and its C++ build tools,
and the existing SharpTestBench ``build/libsharptestbench_host.so``.

The TX/status offsets and flags used by the adapter follow the corresponding
registers in `AMD PG142 <https://docs.amd.com/v/u/en-US/pg142-axi-uartlite>`_.
Only the TX subset described above is supplied by this simulation endpoint.

Artifacts and interpretation
----------------------------

Every run receives a new ``build/sharp-demo/runs/<UTC timestamp>`` directory.
``uart.log`` contains the actual decoded console text. ``simulation.log`` and
SharpTestBench JSON/XML results retain the checks; ``manifest.txt`` records
firmware, model, managed test, native host and RTL source hashes plus compile
options. Build/model logs are retained and failures propagate to CMake.
The runner applies a 15-minute wall-clock bound per subprocess; the C# test
also has a finite simulation-time timeout. A source/options/tool-version hash
controls reuse of the Verilator model, while firmware is loaded fresh each run.
Large generated model/compiler intermediates live in the Linux-native
``~/.cache/nexrt/<workspace-key>/model`` directory. ``NEXRT_SIM_CACHE`` selects
another cache root. Keep this on the Linux filesystem when using WSL; the
source tree and per-run evidence stay in the shared workspace.
``build/sharp-demo/latest-run.txt`` points to the most recent passing run.

This enabled logging/synchronization/metrics example is larger than the nano
configuration. The nano's 6000-byte linked capacity used a 1024-byte boot reserve,
3812 bytes of code/constants and 1164 bytes of BSS including two 512-byte stacks.
The three-worker demo deliberately retains a 4096-byte boot reserve and larger
thread stacks. Its final ELF/map, rather than the nano report, determines its
RAM cost. All firmware images still undergo the static allocation audit.

The initial cross-build measures 9196 bytes of code/constants, 748 bytes of
initialized data and 5536 bytes of BSS. The linked span through ``_bss_end``
is 15488 bytes, including eight bytes of section padding. Adding the 4096-byte
boot reserve gives **19584 bytes of committed RAM**. This includes all three
worker stacks, idle, logger and enabled kernel state. ELF/debug container bytes
and the host simulator are excluded. Rebuild and inspect the map after changes.

Recorded simulation
-------------------

Run ``20261020-004955-899`` passed SharpTestBench ``Entry.ThreeWorkers``
with seed 1 after 88,778,625 simulation steps. Each worker completed at least
three independently checked rounds; 88 machine timer traps occurred without
a fatal CPU fault or unexpected trap. The decoded UART began::

   thread=1 round=1 value=678856385 mask=7
   thread=3 round=1 value=2569212003 mask=7
   thread=2 round=1 value=3771517842 mask=7
   thread=1 round=2 value=3742680769 mask=7
   thread=2 round=2 value=214104978 mask=7
   thread=3 round=2 value=980496483 mask=7

The firmware ELF SHA-256 was
``1cbd755d3914dc622a478f2b473cf45c404065ccebf9e8c98a0b32e7159222fe``.
The run directory retains the full transcript, passing JSON result and manifest
of the exact model, test assembly, native host and RTL inputs. This bounded
simulation checks this integration; it does not qualify board timing, radiation
protection, long-duration operation or worst-case stack use.

.. literalinclude:: ../examples/axi_uart_threads.cpp
   :language: cpp
   :caption: Three workers and the single-owner UART drain

.. literalinclude:: ../sim/rtl/riscue_demo.sv
   :language: systemverilog
   :caption: PLAIN core, AXI endpoint and UART wiring
