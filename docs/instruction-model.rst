Fast RISC-V instruction model
=============================

``sim/interpreter`` runs the actual NexRT ELF on one C++20 RV32 hart. It is a
functional instruction model for fast scheduler, trap and protection testing.
The Linux release build explicitly uses ``-O3``. Firmware remains independently
built at ``-O3``; this host model does not change the firmware or RTL launcher.

Build and run
-------------

From the RTOS repository root in WSL::

   cmake -S sim/interpreter -B build/interpreter -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build/interpreter
   ctest --test-dir build/interpreter --output-on-failure
   build/interpreter/nexrt-rv --elf build/overnight-stress/demo.elf --max-instructions 100000000 --host-seconds 10

Alternatively configure the normal host build with ``-DRTOS_INTERPRETER=ON``.
The standalone build requires no .NET, Verilator or physical board.
``--output FILE`` saves the same JSON report printed to standard output.
UART TX register writes are displayed as ``[uart]`` lines on stderr, keeping
stdout available for the JSON result. ``--uart-log FILE`` preserves the exact
transmitted bytes. The fast model uses an immediate transmit sink; use the RTL
demo for AXI backpressure and UART pin/8N1 checks::

   build/interpreter/nexrt-rv --elf build/uart/demo.elf --max-instructions 10000000 --uart-log build/interpreter/uart.log

Processor profiles
------------------

The default preserves M-only execution, compressed instructions, RV32M and the
RISCue bit manipulation subset (Zbb, Zbc and Zbs). CSR access, instruction fences,
counters, synchronous traps, machine timer/software interrupts and an externally
driven interrupt input are modeled. RAM starts at zero (32 KiB by default), with
the existing RISCue CLINT addresses and an immediate UART transmit sink.

``--user-pmp`` separately enables U mode, MPRV, TW, counter permissions and four
PMP regions with TOR, NA4, NAPOT, locking and whole-access matching. The default
profile does not enable this extension. ``--zaamo`` independently enables the
nine word AMOs and the existing feature-discovery CSR. Zaamo does not imply the
full A extension: LR/SC remain unsupported and ``misa.A`` stays clear.
Both switches can be combined. ``--no-compressed`` and ``--no-bitmanip`` select
reduced instruction profiles. This initial backend has no supervisor mode, MMU,
floating point, DMA device model or multiple harts.

All modeled threads share one hart. The actual firmware scheduler switches their
contexts through its normal traps and interrupt handlers. Independent processes
can use separate RAM and ELF instances for parallel experiments; a single hart's
instruction stream remains sequential.

Timer and speed interpretation
------------------------------

By default each instruction boundary or interrupt entry advances the virtual
timer by one cycle. ``--cycles-per-instruction N`` selects another fixed cost.
The interrupt condition is checked every boundary, including inside batches.
``--clock-hz`` defaults to 100 MHz and must match the clock identity in recognized
NexRT stress firmware. Host time never drives the guest timer.

WFI defaults to a NOP, preserving RISCue behavior. Explicit ``--wfi-sleep`` skips
idle virtual cycles to the next enabled timer compare. It still executes the
real interrupt handler and records every skipped cycle. This option changes WFI
behavior and must be identified when comparing results::

   build/interpreter/nexrt-rv --elf build/overnight-stress/demo.elf --wfi-sleep --target-ticks 10000 --host-seconds 10 --max-instructions 1000000000

JSON distinguishes host wall seconds, virtual seconds, retired instructions and
skipped cycles. This model does not predict pipeline throughput, peripheral bus
latency, ECC/TMR fault behavior or hardware race timing. Keep RTL tests for those
properties. A shorter host runtime does not by itself prove a target-duration soak.

Recognized stress ELFs are monitored for their phase and failure code. Unexpected
synchronous faults remain sticky across observation batches. Atomic completion
also checks the expected ticket, CAS, guard and feature results. Instruction,
host-time and target-tick bounds report explicit incomplete outcomes, not a pass.
Host/target bounds are observed after batches of up to 4096 boundaries, so a run
can overshoot a bound by one batch. Exit code 1 means firmware failure; 2 means
an input or host error. Exit code 0 includes bounded incomplete outcomes: inspect
the JSON ``outcome`` before declaring completion.

Verification
------------

The native tests cover seeded arithmetic, multiply/divide edge cases, bit
operations, AMOs and their ordering encodings, CSR permissions, precise traps,
timer boundaries, M/U transitions, MPRV, PMP bounds/locks and compressed decoding.
Counter writes replace implicit instruction retirement increments, following the
`RISC-V CSR rules <https://docs.riscv.org/reference/isa/v20240411/_attachments/riscv-unprivileged.pdf>`_.
ELF loading checks the executable type, ABI, ranges, overlap and symbol layout.

Existing independent RISCue protection fixtures can be replayed without changing
or rebuilding the hardware checkout::

   build/interpreter/nexrt-rv-tests --rtl-fixtures /mnt/c/dev/RISCue/build/user-pmp-120

This checks all 71 applicable compressed/uncompressed fixture signatures, trap
counts and kernel guards, including external interrupt injection. The fixture
directory is optional external test evidence, not a dependency of the normal
build. Host sanitizers can be enabled in a separate build directory using
``-DNEXRT_INTERPRETER_SANITIZERS=ON``.
