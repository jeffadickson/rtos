Configuration reference
=======================

Configure with ``cmake --preset host -DOPTION=VALUE`` or the firmware preset.
Settings are propagated through ``rtos_options``. Application targets should
link ``rtos`` or ``rtos_ethernet`` so they inherit the same definitions.
Never compile different translation units with different feature definitions:
``RTOS_METRICS`` and ``RTOS_SYNC`` change the public thread object's layout.
Capacities and tick rates use positive decimal integers without leading zeroes;
expressions and lists are rejected during configuration. Timer addresses may use
hexadecimal. Every configured capacity still has to fit the selected board's RAM.

The supplied host and firmware presets select ``Release``. With the supported
GNU/Clang toolchains this uses ``-O3`` for kernel, optional modules and application
code; no kernel-specific ``-Os`` override is applied. Host test assertions remain
enabled at this optimization level. Reconfigure existing build directories with
their preset before rebuilding. Inspect ``compile_commands.json`` in the build
directory for the actual per-file compiler flags. An explicitly selected build
type or custom compiler flags can change the optimization level.

.. list-table:: Implemented configuration
   :header-rows: 1
   :widths: 28 15 57

   * - CMake setting
     - Default
     - Contract
   * - ``RTOS_MAX_THREADS``
     - 16
     - 1..255 application threads; idle is additional.
   * - ``RTOS_TICK_HZ``
     - 1000
     - 1..UINT32_MAX default rate; ``start`` may supply another rate.
   * - ``RTOS_IDLE_STACK_BYTES``
     - 1024
     - At least 512 bytes; multiple of 16.
   * - ``RTOS_BOOT_STACK_BYTES``
     - 4096
     - RISCue boot reserve, 512..32512 bytes, multiple of 16; qualify reductions for the startup workload.
   * - ``RTOS_ROUND_ROBIN``
     - ON
     - Rotate equal priorities on timer interrupts; yield always rotates.
   * - ``RTOS_METRICS``
     - ON
     - Track per-thread runtime and dispatch totals; snapshots still list threads when OFF.
   * - ``RTOS_STACK_GUARD``
     - ON
     - Bottom sentinel check at dispatch; detection, not prevention.
   * - ``RTOS_ETHERNET``
     - ON
     - Build the optional static polling adapter, not a hardware MAC driver.
   * - ``RTOS_WEB``
     - ON
     - Bounded read-only HTTP component; transport and TCP/IP integration are separate.
   * - ``RTOS_SYNC``
     - OFF
     - Static queues, semaphores, events, priority-inheriting mutexes and single-hart atomic words.
   * - ``RTOS_LOGGING``
     - OFF
     - Bounded queued formatting; requires synchronization. UART callback is board supplied.
   * - ``RTOS_IPC_MAX_MESSAGE_BYTES``
     - 128
     - 1..512 bytes per copied queue value, including logger record metadata.
   * - ``RTOS_MAX_HELD_MUTEXES``
     - 8
     - 1..255 simultaneously owned mutexes per thread.
   * - ``RTOS_MTIME_ADDRESS``
     - 0x0200bff8
     - Aligned address of two 32-bit machine timer words.
   * - ``RTOS_MTIMECMP_ADDRESS``
     - 0x02004000
     - Aligned address of two 32-bit compare words.
   * - ``RTOS_BUILD_FIRMWARE``
     - OFF
     - OFF selects native tests; ON requires cross-toolchain configuration.

Headers also provide defaults for non-CMake consumers. Header boolean settings
must be 0 or 1. Address correctness and available memory remain board-level
responsibilities. The default Ethernet frame capacity is a compile-time constant
of 1536 bytes and RX poll budget is a per-call parameter.
``RTOS_STORAGE`` remains OFF; selecting ON fails explicitly until the flash
service is implemented. CMake checks idle-stack alignment and representability;
the linker and board integration establish whether the requested storage fits.

Recommended profiles
--------------------

Development enables metrics and guards. The ``minimal`` preset disables
metrics, guards, time slicing, and the Ethernet library to verify feature removal.
Production configuration should be selected from measured memory, timing, and
diagnostic requirements rather than simply disabling every diagnostic feature.

Future configurable services
----------------------------

These are proposed extensions, not currently available options:

.. list-table:: Extension backlog
   :header-rows: 1
   :widths: 28 36 36

   * - Service
     - Proposed configuration
     - Acceptance condition
   * - Time services
     - Absolute periodic delay, tickless idle, static callback timers
     - Drift, wrap, cancellation and interrupt-latency tests
   * - Diagnostics
     - Stack high-water marks, trace ring, fault records, execution budgets
     - Bounded collection cost and retained-fault-data tests
   * - Boot and recovery
     - Ordered init tables, watchdog supervision, reset reason, restart policy
     - Partial-start failure and watchdog fault-injection tests
   * - Networking
     - Static frame pools, MAC interrupt deferral, VLAN/MTU, stack adapter
     - Ownership, exhaustion, link flap and packet-loss tests
   * - Portability
     - Port interface, interrupt controller hooks, memory protection
     - Full context/ABI and isolation verification for each port

The synchronization and messaging services above are implemented for the
single-hart model and cross-built RISCue port. Physical peripheral interrupt
dispatch, UART/MAC drivers, DMA, PCIe and other CPU ports remain integration work.

Avoid making unverified features selectable. Each extension should define its
storage budget, boot lifecycle, failure behavior, and tests before exposure as
a public configuration option.
