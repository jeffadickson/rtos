Tang Nano 9K RISC-V port work package
================================================================================

Status: board/CPU contract scaffold; firmware remains provisional and disabled.
The board alone does not select a CPU. The existing target identity explicitly
means a proposed RISCue RV32IM/Zicsr soft SoC, not every Tang Nano bitstream.

Use ``config/boards/tang-nano-9k.example.json`` to capture the bitstream identity,
internal RAM, actual machine-timer frequency and timer/compare register addresses.
Unknown values stay null. The board oscillator must not be substituted for a
synthesized CPU/timer clock. Keep contexts/stacks in deterministic internal RAM
until external PSRAM timing and initialization are qualified.

Architecture implementation order
--------------------------------------------------------------------------------

1. Identify the actual core, version/configuration and hardware-design revision.
2. For a matching RISCue machine-mode core, verify CSR/ECALL/mret and split timer
   behavior against ``ports/riscue`` before sharing its CPU boundary. Board startup,
   linker map, clock/reset and MMIO adapter must come from that hardware design.
3. For PicoRV32, implement its custom IRQ/q-register/retirq contract separately;
   the current machine-mode vector cannot be reused by changing addresses.
4. For another machine-mode core such as NEORV32, review its SoC timer and IRQ
   contract instead of assuming RISCue's peripheral map.
5. Add explicit board boot/reset, timer acknowledgement, UART/GPIO and later flash
   adapters, then cross-build and qualify preemption/register/stack behavior.

The native hardware validator accepts only the staged RISCue contract today and
reports an alternate core as requiring a different adapter. Passing metadata
validation neither enables firmware nor proves a correct FPGA memory map.
