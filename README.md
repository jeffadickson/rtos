# RISCue RTOS

A small C++17 RTOS with static boot allocation, preemptive priorities, optional
round-robin time slicing, sleep/yield/exit, runtime priority updates, and thread
snapshots with CPU residency counters. Inline assembly handles CSR operations;
a small `.S` boundary preserves all registers during preemption.

## Build

```sh
cmake --preset host
cmake --build --preset host
ctest --preset host
cmake --preset riscue -DRISCV_TOOLCHAIN_PREFIX=/path/to/riscv64-unknown-elf-
cmake --build --preset riscue
```

CMake 3.22+, Ninja, .NET 10 SDK, and C++17 are required. Custom tooling uses C#
with generated XML documentation (XDoc). Python is confined to the Sphinx
documentation dependency and its minimal configuration. The `minimal` preset tests
optional feature removal. The firmware build runs an allocator-symbol audit;
the linker rejects runtime C++ constructors. Kernel storage cannot grow after
boot. Application objects, stacks, arguments, and names must remain alive.

## Manual

The detailed Sphinx user manual is in [docs/index.rst](docs/index.rst). It covers
boot, APIs and errors, configuration, CPU usage calculation, memory ownership,
RISCue assembly/ABI, driver inventory, Ethernet integration, requirements and
verification, and five engineering review iterations.

```sh
python3 -m venv build/docs-venv
build/docs-venv/bin/python -m pip install -r docs/requirements.txt
build/docs-venv/bin/python -m sphinx -n -W --keep-going -b html docs build/manual/html
```

The optional Ethernet library provides a fixed-buffer interface for existing
MAC and protocol code. Board MAC/PHY drivers and TCP/IP integration remain
application/board work. No heap is used by the kernel or adapter; third-party
buffer ownership still needs its own audit.

Host tests and successful cross-builds do not establish interrupt correctness
on RTL or certification. The manual lists the pending target verification gates.

## Modular configuration and targets

Select a board/CPU and independent feature profile using the C# configurator:

```sh
cmake --preset host
cmake --build --preset host --target configure_tool
build/host/native-tools/NexRT.Configure/NexRT.Configure list .
build/host/native-tools/NexRT.Configure/NexRT.Configure generate . riscue minimal build/config/minimal.cmake
cmake --preset riscue -C build/config/minimal.cmake
```

See [platforms and multiprocessing roadmap](docs/platforms.rst). RISCue is the
only cross-built firmware port. VORAGO, AMD/Xilinx ARM, MicroBlaze and Tang Nano
9K entries are researched planned/provisional profiles; firmware generation
fails explicitly until each port and BSP exists. AMP/SMP are designed for but
currently rejected.

## Static communication and queued printf

Enable `RTOS_SYNC` for typed queues, events, counting semaphores,
priority-inheriting mutexes and single-hart atomic words. Add `RTOS_LOGGING`
for a bounded `console.printf` interface and serialized, budgeted output.
Both are off in nano. The RV32 `Logger<8,64>` object occupies 720 bytes,
including its queue, pending record and controls; producer formatting uses
existing thread stack space. It does not override libc `printf`.

```sh
cmake --preset logging -DRISCV_TOOLCHAIN_PREFIX=/path/to/riscv64-unknown-elf-
cmake --build --preset logging
```

The logging example uses a counting sink, not a physical UART driver.
See [the synchronization manual](docs/synchronization.rst) and
[100 implementation steps](docs/sync-iterations.rst). Host checks and RV32
cross-links pass; external peripheral interrupts and board qualification remain
outstanding. These APIs are single-hart, not SMP synchronization.
