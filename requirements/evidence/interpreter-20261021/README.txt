NexRT C++20 instruction model verification, 2026-10-21

Release compiler: GCC 13.3, explicit -O3; see compile_commands.json.
115,289 directed/seeded architectural checks passed.
71 independent RISCue M/U/PMP ELF fixtures passed (both compressed settings).
Address/undefined-behavior sanitizer tests and fixture replay passed.
Sanitized Zaamo firmware reached completed with 968 AMOs and no failure.
Baseline and Zaamo firmware reached completed with expected tickets/CAS/guards.
Wrong-clock firmware was rejected before execution; Zaamo firmware without the
Zaamo model option reported firmware failure 12 rather than false completion.
Standalone and optional top-level host CMake builds passed.
Requirements tracker check and strict Sphinx documentation build passed.

five-minute-soak.json records 300.03018903 virtual seconds in 24.148745287
host seconds, 17,960 jobs, all workers/dispatch counters active, and no firmware
failure. This explicitly enabled WFI sleep and used one virtual cycle per step.
It stopped at a target-tick bound, not the configured 12-hour firmware endpoint.
The JSON outcome correctly says target_limit_reached rather than completed.

mu-soak.json used default WFI-as-NOP with optional U/PMP enabled and actual
M-mode RTOS firmware: 100 million boundaries, 1 virtual second, no failure.
The independent RTL fixtures exercise actual U-mode transitions and PMP denial.
The atomic-zaamo.json run enables BOTH optional U/PMP and Zaamo.

This is a functional model. It does not validate pipeline timing, hardware ECC,
TMR, peripheral bus timing or a DMA device. The existing overnight RTL process
was verified alive and left unchanged; its long soak is separate evidence.
No physical hardware was programmed, and no commit or push was performed.
