Modular configuration and platform roadmap
==========================================

Configuration workflow
----------------------

``config/system.json`` is the target/profile catalogue. Select a CPU/board and
an independent feature profile instead of editing scattered headers. The C#
configurator validates capacities, identities and multiprocessing settings and
writes a deterministic CMake initial cache. All custom tooling uses XDoc.

.. code-block:: sh

   cmake --preset host
   cmake --build --preset host --target configure_tool
   build/host/native-tools/NexRT.Configure/NexRT.Configure list .
   build/host/native-tools/NexRT.Configure/NexRT.Configure generate . riscue minimal build/config/minimal.cmake
   cmake --preset riscue -C build/config/minimal.cmake
   cmake --build --preset riscue

The RISC-V compiler must be on PATH, or supply
``-DRISCV_TOOLCHAIN_PREFIX=/path/to/riscv64-unknown-elf-`` during CMake
configuration. The target catalogue does not install compilers or assume local
SDK paths.

Use a fresh build directory when changing compiler/architecture. Generated
cache files force the selected feature values; regenerate them to change a
profile. Existing ``-D`` configuration remains available without an initial
cache. ``minimal`` disables optional metrics, guards, slicing and Ethernet;
``default`` enables them; ``diagnostic`` enables diagnostics without Ethernet.
Ethernet means the fixed-buffer adapter, not an implemented IP stack or MAC.
No profile introduces runtime allocation or runtime module loading.

Module boundaries
-----------------

* ``nexrt::options`` carries shared public compile definitions and include paths.
* ``nexrt::kernel`` is the firmware scheduler/runtime, mandatory in firmware.
* ``nexrt::ethernet`` is independently selectable with ``RTOS_ETHERNET``.
* ``ports/riscue/rtos_port.hpp`` owns CSR instructions, context initialization,
  timer MMIO, interrupt masking and trap classification. Context assembly and
  startup remain in the architecture port. The scheduler has no inline assembly.
* Board support must own clocks, pins, memory map, interrupt routing, console,
  reset and MAC/PHY. Static IPC is implemented; VFS, shell, network stack and
  additional drivers remain separately budgeted optional modules.

The extraction is an initial port boundary, not a claim of a completed generic
32/64-bit port ABI. Thread context storage and the trap callback currently use
32-bit words. AArch64 requires a reviewed pointer-width-neutral context ABI,
frame offsets and public-layout compatibility tests before implementation.

Researched targets and status
-----------------------------

RISCue cross-builds and has PLAIN-core UART RTL simulation evidence; see
:doc:`uart-demo`. The separate QEMU Cortex-M3 simulation BSP is described in
:doc:`arm-stress`. Neither establishes physical board qualification.
Every other entry is a planned port: configuration can be generated and inspected,
but CMake deliberately rejects firmware generation before compiler selection.
No ARM or MicroBlaze assembly is substituted with RISC-V instructions.

.. list-table:: Target catalogue
   :header-rows: 1
   :widths: 26 24 50

   * - Profile
     - CPU
     - Port work and scope
   * - riscue
     - RV32IM + Zicsr
     - One machine-mode hart; PLAIN RTL UART demo; hardware qualification pending.
   * - qemu-mps2-m3
     - Cortex-M3
     - Dedicated QEMU AN385 simulation BSP with SVC/SysTick, BASEPRI and integer
       PSP frames. This is not a VORAGO or Zynq board port.
   * - vorago-va108xx
     - Cortex-M0
     - Provisional interpretation of "virago". PendSV/SVC, NVIC, timer,
       vectors, startup, exact part memory map and BSP required.
   * - vorago-va416xx
     - Cortex-M4
     - Provisional VORAGO family. Same Cortex-M port work plus exact optional
       floating-point ABI, fault/EDAC and peripheral configuration review.
   * - zynq7000-a9
     - Cortex-A9
     - ARMv7-A exceptions, GIC, timer, cache/MMU policy, boot handoff and BSP.
       7000S has one core; dual-core configuration applies to other 7000 parts.
   * - zynqmp-r5
     - Cortex-R5F
     - Real-time ARMv7-R port; MPU/TCM, GIC, timer and split/lockstep boot policy.
   * - zynqmp-a53
     - Cortex-A53, AArch64
     - Separate ARMv8-A port, exception level, GIC, generic timer, cache/MMU and
       64-bit context ABI. Exact part determines two versus four A53 cores.
   * - microblaze
     - Classic 32-bit MicroBlaze
     - Separate ISA, compiler ABI, vectors, AXI interrupt controller/timer,
       cache/barriers and BSP generated from the selected hardware design.
   * - tang-nano-9k-riscue
     - Assumed RISCue soft core
     - Provisional board/core pairing. Tang Nano 9K is an FPGA board, not a fixed
       CPU ISA. Select bitstream, core, CSR/timer/IRQ contract and memory map.

Physical core limits in the catalogue describe a family upper bound, not an
assertion about a chosen part or RTOS multiprocessing support. MicroBlaze is
not MicroBlaze V (RISC-V); a V port needs its own hardware configuration.
VORAGO M0/M4 are separate architecture profiles and require exact part/board
selection before clocks, interrupts or addresses are generated. No guessed
register addresses or linker maps are emitted for planned profiles.

Tang Nano integration
---------------------

The Sipeed board uses a Gowin GW1NR-9 FPGA. Its oscillator is not automatically
the CPU or timer frequency: use the synthesized PLL and SoC design. The assumed
RISCue configuration must provide machine-mode trap/CSR behavior and a timer
compatible with the port, or a new timer/interrupt adapter. A PicoRV32 design
with custom IRQ instructions is not interchangeable with standard machine-mode
RISC-V. Board PSRAM requires its own controller, initialization, timing and
bounded-access evidence; it is not automatically suitable for kernel stacks.
Keep the kernel, contexts and critical stacks in deterministic internal memory
unless external-memory timing is qualified. UART, GPIO, watchdog and optional
SPI flash/PSRAM drivers depend on the actual bitstream memory map.

Multiprocessor architecture
---------------------------

``RTOS_CPU_COUNT`` and ``RTOS_MULTIPROCESSING`` reserve explicit topology
configuration. Today only ``1`` and ``up`` are accepted by both the C# tool and
CMake; requesting AMP/SMP fails rather than running an unsafe shared scheduler.

Future AMP uses independent boot-reserved kernel instances, partitioned memory,
IRQ/timer ownership and bounded shared-memory mailboxes. It needs separate
images/boot handoffs and cache/visibility rules. It is the initial recommendation
for heterogeneous A53/R5 and FPGA processor combinations. R5 lockstep represents
one logical execution context, not two independently schedulable CPUs.

Future SMP requires per-CPU current/idle threads, scheduler and IRQ stacks,
affinity, deterministic global-priority policy, scheduler spinlocks, remote
reschedule IPIs, atomic publication and acquire/release barriers. Local interrupt
masking is not a cross-CPU lock. Every shared pool needs conservation under
concurrent ownership, cache-coherent or explicit noncoherent protocols, and
bounded lock/interrupt latency. Per-CPU metrics must define synchronized time
and migration accounting. Boot barriers, CPU offline policy, FP/TLS state and
fault containment require explicit contracts. No heap or dynamic CPU/thread
creation is introduced by either model. Heterogeneous ISAs use AMP, not one
shared SMP register-frame scheduler.

Port acceptance
---------------

Each port needs context canary/register tests, nested interrupt policy, timer
rollover/catch-up, priority/preemption, compiler helpers, stack bounds, DMA/cache
ownership, no-heap and constructor audits, plus exact-board linker overflow
fixtures. Cross-compilation and host configuration tests do not qualify a port.

Research sources
----------------

* `VORAGO development boards <https://www.voragotech.com/products/development-tools>`_
  identifies Cortex-M0 VA108xx and Cortex-M4 VA416x0 platforms.
* `VORAGO VA41630 <https://www.voragotech.com/products/microcontrollers/arm-cortex-m4-family/va41630>`_
  provides the selected family example and peripheral overview.
* `AMD Zynq-7000 <https://www.amd.com/en/products/adaptive-socs-and-fpgas/soc/zynq-7000.html>`_
  distinguishes single-core 7000S and dual-core Cortex-A9 devices.
* `AMD Zynq UltraScale+ <https://www.amd.com/en/products/adaptive-socs-and-fpgas/soc/zynq-ultrascale-plus-mpsoc.html>`_
  identifies A53 and R5F subsystems; exact SKU controls topology.
* `AMD MicroBlaze <https://www.amd.com/en/products/software/adaptive-socs-and-fpgas/microblaze.html>`_
  describes the configurable soft processor; select exact generated ABI/options.
* `MicroBlaze endianness <https://docs.amd.com/r/2024.1-English/ug984-vivado-microblaze-ref/Data-Types-and-Endianness>`_
  documents configuration-dependent data representation.
* `Sipeed Tang Nano 9K <https://wiki.sipeed.com/hardware/en/tang/Tang-Nano-9K/Nano-9K>`_
  specifies the FPGA board; the selected soft SoC defines CPU and peripherals.
