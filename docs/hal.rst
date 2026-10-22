Processor hardware abstraction layer
================================================================================

The kernel calls ``rtos::hal`` in ``include/rtos_hal.hpp``. This compile-time
interface forwards to the selected ``rtos::port`` provider. It introduces no
virtual dispatch, runtime registration, heap allocation or additional state.
Processor feature options and simulation runners remain separate from this
mandatory kernel boundary. Applications continue to use ``rtos.hpp`` rather
than selecting architectural instructions themselves.

CMake selects ``ports/riscue`` for RISCue RV32 and ``ports/arm-mps2`` for the
Cortex-M3 QEMU MPS2 target. An unimplemented target is rejected; it never falls
back to another processor's port. Planned catalogue entries still require their
own implementation and verification before firmware builds are enabled.

Port provider contract
----------------------

Each provider supplies ``rtos_port.hpp`` on the kernel's private include path:

* Save/mask and restore local scheduling interrupts, preserving nested masks.
  Synchronous scheduling must remain callable while this mask is held.
* Read the machine timer and arm an absolute deadline in matching timer units.
  Any counter-extension state is accessed under kernel serialization.
* Construct an initial context frame in boot-static aligned stack storage.
* Install exception vectors and configure/enable the scheduling timer.
* Enter the synchronous scheduling exception, classify yield/timer causes and
  advance the saved PC where required by that processor's exception behavior.
* Supply idle and fail-closed halt behavior.

The provider also needs matching startup/context assembly, linker layout, BSP
register addresses and clock contracts. Moving inline instructions behind this
HAL does not make them portable to an unimplemented processor. The current
saved-frame API uses 32-bit words; current processor mask types are 32 bits,
transported through ``uintptr_t`` by synchronization. A 64-bit port needs an
explicit ABI review. Local interrupt masking does
not establish SMP synchronization, DMA exclusion or cache coherence.

Verification and optional features
--------------------------------------------------------------------------------

The host ``processor_hal`` fixture substitutes a test processor provider with
different exception identities. It checks nested saved masks, 64-bit timer
values, frame/entry forwarding, vector/timer setup and rescheduling. This is a
contract test, not an instruction-level processor simulation. RISCue firmware
cross-builds and the Cortex-M3 port must retain their separate target checks.

Optional ISA extensions and emulator integrations must use their own options,
targets and build directories. Enabling one must not silently change the base
HAL provider, baseline compiler ISA, memory endpoint or existing test workload.
Keep baseline and feature-enabled evidence separate.

The first optional word-RMW provider is described in :doc:`hardware-atomics`.
It is default-off and does not replace this kernel HAL.

.. literalinclude:: ../include/rtos_hal.hpp
   :language: cpp
   :caption: Compile-time processor HAL
