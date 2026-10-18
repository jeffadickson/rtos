Configuration reference
=======================

Configure with ``cmake --preset host -DOPTION=VALUE`` or the firmware preset.
Settings are propagated through ``rtos_options``. Application targets should
link ``rtos`` or ``rtos_ethernet`` so they inherit the same definitions.
Never compile different translation units with different feature definitions:
``RTOS_METRICS`` changes the public thread object's layout.

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
     - Positive default rate; ``start`` may supply another rate.
   * - ``RTOS_IDLE_STACK_BYTES``
     - 1024
     - At least 512 bytes; multiple of 16.
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
   * - Synchronization
     - Static mutexes with priority inheritance, counting semaphores, event bits
     - Inversion, timeout, ISR rules and owner-death tests
   * - Messaging
     - Typed fixed-capacity queues, ISR-to-worker notification
     - Bounded operations; full/empty and overflow policy tests
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

Avoid making unverified features selectable. Each extension should define its
storage budget, boot lifecycle, failure behavior, and tests before exposure as
a public configuration option.
