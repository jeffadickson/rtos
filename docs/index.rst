RISCue RTOS User Manual
=======================

Version 0.2.0. Engineering baseline, 18 October 2026.

This manual describes the implemented kernel, its configuration, operating
contracts, integration boundaries, and verification obligations. It is written
to support disciplined embedded development and later qualification work.
It does not establish certification, compliance with a safety standard, a
worst-case execution time, or hardware validation.

.. toctree::
   :maxdepth: 2
   :numbered:

   simple-start
   minimal-application
   uart-demo
   arm-stress
   how-it-works
   getting-started
   architecture
   assembly-port
   source-reference
   configuration
   platforms
   port-bringup
   api
   metrics
   stack-observations
   memory
   diagnostic-overlays
   drivers
   web
   sync-iterations
   synchronization
   simplicity-iterations
   correctness-iterations
   implementation-300
   verification
   reviews
   validation
   tooling
   footprint
   integration-history
   requirements-tracker
   requirements-process

Scope and status
----------------

The target is one RISCue RV32 machine-mode hart. Implemented services are
preemptive fixed-priority threads, optional equal-priority time slicing,
relative sleep, yield, thread completion, runtime priority updates, diagnostics,
an optional polling Ethernet integration adapter, and optional static IPC,
priority-inheriting mutexes and bounded queued logging. Hardware-specific MAC,
PHY, UART, and other peripheral drivers are integration work.

All kernel storage is reserved statically before scheduling begins. Thread
registration occurs at boot and is sealed by ``start()``. There is no kernel
allocation, thread deletion, dynamic stack growth, or runtime object creation.

Terminology
-----------

* **Tick:** one logical scheduling interval; unrelated to a CPU cycle count.
* **Cycle:** one increment of the RISCue machine timer at the configured core clock.
* **Ready:** eligible to execute; the running thread also has ready state.
* **Idle:** private fallback thread selected only when no application is ready.
* **Static allocation:** storage capacity fixed before execution; no general heap.
* **Guard:** a sentinel that detects some stack corruption; not memory protection.

External references
-------------------

The build follows `CMake presets documentation <https://cmake.org/cmake/help/v3.22/manual/cmake-presets.7.html>`_.
Documentation uses `Sphinx's command-line builder <https://www.sphinx-doc.org/en/master/man/sphinx-build.html>`_.
The Ethernet integration discussion refers to the upstream
`lwIP NO_SYS contract <https://www.nongnu.org/lwip/2_1_x/group__lwip__opts__nosys.html>`_
and `lwIP initialization sequence <https://www.nongnu.org/lwip/2_1_x/sys_init.html>`_.
