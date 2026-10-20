Optional DMA channels
================================================================================

The portable DMA ownership layer is implemented in ``rtos_dma.hpp`` and
``src/dma.cpp``. The engine-specific register, interrupt and cache adapter is
supplied by the BSP. No particular FPGA IP or board DMA execution is claimed.
Initial operation is polled; external IRQ dispatch is still separate work.

Enable and link only where needed
--------------------------------------------------------------------------------

``RTOS_DMA`` defaults OFF. Disabled builds create no DMA target, channel,
buffer, worker, interrupt handler or kernel-control fields. Enable synchronization
explicitly because runtime calls serialize through the existing thread mutex:

.. code-block:: sh

   cmake -S . -B build/host-dma -G Ninja -DRTOS_DMA=ON -DRTOS_SYNC=ON
   cmake --build build/host-dma
   ctest --test-dir build/host-dma --output-on-failure

Firmware applications using the API link ``nexrt::dma`` alongside
``nexrt::kernel``. Merely enabling the target does not create channel objects or
buffers. The nano preset keeps DMA disabled by default. To exclude it explicitly,
configure ``-DRTOS_DMA=OFF``. There is no DMA dependency in the kernel.

The repository's firmware ``demo`` links the optional library when DMA is
enabled; unused DMA objects are not pulled from the archive. For another CMake
consumer use ``target_link_libraries(app PRIVATE nexrt::dma nexrt::kernel)``.
``examples/dma_polling.cpp`` is buildable with ``RTOS_APPLICATION`` selecting it
and both DMA/SYNC enabled. It uses a **software adapter**, debugger-visible
pass/failure counters and copied TX staging; it does not operate a DMA engine.
Replace its adapter and timer frequency before using physical hardware.

Boot-static storage and use
--------------------------------------------------------------------------------

Reserve one permanent Channel and one exclusive aligned staging allocation per
hardware channel. There is no global pool or default buffer size. The cost is
``sizeof(rtos::dma::Channel)`` plus the chosen staging capacity and alignment
padding; both depend on the target ABI/configuration. TX and RX use separate
channels and allocations. Do not overlap backing allocations between channels.
If SYNC was previously disabled, enabling it also adds its existing per-thread
wait/mutex metadata and synchronization code. Measure the whole selected image,
not just the DMA channel object, when comparing with the nano configuration.

.. code-block:: cpp

   #include "rtos_dma.hpp"
   static rtos::dma::Channel tx;
   alignas(64) static uint8_t staging[256];
   // BSP supplies callbacks and an independently mapped bus address.
   bool bind_tx(rtos::dma::Backend engine, uint64_t mapped_bus) {
       return tx.initialize(engine, {staging, mapped_bus, sizeof(staging), 64},
                            rtos::dma::Direction::transmit);
   }
   // In a running application thread, after bind_tx() at boot:
   void submit_example() {
       const uint8_t command[]{1, 2, 3, 4};
       rtos::dma::Ticket ticket;
       if (tx.transmit(command, sizeof(command), ticket) != rtos::dma::Status::ok)
           return;
       // command may now leave scope: hardware sees only permanent staging.
       // Keep ticket in application-owned state; a service thread can poll it.
   }

Initialization is single-caller boot work and may bind each object only once.
The buffer, channel, engine state and callback code must outlive scheduling.
The bus address is never obtained by casting the CPU pointer. Alignment must
cover the engine, cache line and memory-system requirements; capacity is a
multiple of that alignment, and every touched cache line belongs exclusively
to staging. Applications must not access staging directly after binding.

Runtime operations are thread-only, single-CPU and nonblocking on contention:
another caller gets ``busy``. The mutex is held only during one API call, with
copy/cache work outside the driver's interrupt masking. Call these APIs with
interrupts enabled; the driver does not override a caller's existing mask.
An active DMA transfer holds staging
ownership, not a thread mutex. No caller source/destination pointer is retained.
Argument/output storage must be ordinary application RAM, separate from channel
control/staging and other concurrent accesses. Do not terminate a thread inside
a driver callback or an API call. A submitting thread may exit after return;
permanent staging remains owned and a supervisor can poll/cancel its ticket.

Completion, cancellation and recovery
--------------------------------------------------------------------------------

One operation is active per channel. ``transmit`` copies input before launch;
``receive`` accepts only a maximum length, with no caller output pointer.
``poll`` performs one backend observation and reports pending, completion or
failure. Completion must prove that the engine has finished all bus accesses.
After cache completion, ``collect`` copies a completed RX result into a caller
buffer, or retires TX with null output and zero capacity. A short RX is allowed.
Insufficient output capacity preserves caller output/count and the completed
result, allowing retry. Successful collection releases the channel.

Each accepted launch has a channel-local generation ticket. Old tickets and
stale backend completion indications cannot finish a newer transfer. Adapters
for hardware without cookies must drain/acknowledge stale status before launch
and associate observations with the current generation. Generation exhaustion
returns ``exhausted`` instead of wrapping; reinitialization cannot reset it.

Applications enforce deadlines using kernel ticks and bounded polling/sleeping.
On deadline expiry call ``cancel``. A successful stop proof discards the transfer
and returns ``cancelled``. If stopping is uncertain, the channel becomes
``quarantined``: no copy-out, relaunch or cache finish occurs. Only ``recover``
with the current ticket can retry stop/drain and release the allocation after
positive quiescence proof. It never returns uncertain RX data as valid.
An engine error uses the same stop/quarantine policy; a safely stopped failed
transfer can be retired with ``collect`` or ``cancel``.

No descriptor chains, cyclic DMA, zero-copy caller loans, automatic owner-exit
cancellation, IRQ wakeups or PCIe link-reset recovery are supplied initially.
``snapshot`` reports state, generation, requested/transferred bytes and capacity
under the same mutex. Tickets are identities, not access-control capabilities.

Writing the hardware adapter
--------------------------------------------------------------------------------

All Backend callbacks are synchronous, bounded, nonblocking and nonreentrant.
They must not yield, exit or call back into kernel services that suspend.

* ``accepts`` validates the full reserved region at binding and actual transfer
  lengths at launch: bus windows, address width, direction and length rules.
* ``prepare`` performs the required cache operations and release/device ordering
  after TX copying and before hardware launch. Coherent/noncached systems still
  supply explicit callbacks with the appropriate ordering; no generic fence is
  assumed sufficient.
* ``start`` programs one operation using only staging's bus address. A false
  return guarantees no command was issued and hardware retained no reference.
* ``poll`` returns the associated ticket and progress. Completed means drained
  bus accesses and a trustworthy actual byte count, not simply an interrupt bit.
* ``stop`` returns true only when all outstanding accesses are drained. Clearing
  a run bit, timing out a reset, or losing a PCIe link alone is not proof.
* ``finish`` runs only after completion or proven stop, performs receive cache
  maintenance and acquire ordering, and makes staging safe for CPU reuse.

Do not use a channel-wide reset that can disrupt another active direction
without a shared reset/ownership protocol. Stalled AXI accesses need hardware
timeouts/watchdogs; a software polling budget cannot bound a nonreturning MMIO
read. The portable layer does not implement IOMMU protection or SMP coherence.

Verification
--------------------------------------------------------------------------------

The enabled host fixture uses a simulated adapter to check cache/launch ordering,
bus/CPU address separation, copied TX lifetime, RX output preservation, stale
generation handling, contention, failed launch, abort uncertainty, recovery and
invalid completion sizes. Sanitizer/Memcheck builds include it when DMA is
enabled. These checks exercise the ownership implementation, not an FPGA engine.

.. literalinclude:: ../include/rtos_dma.hpp
   :language: cpp
   :caption: Portable DMA API and BSP callback contract
