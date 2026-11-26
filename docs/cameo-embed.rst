Cameo.Embed Static C++20 VM
===========================

``RTOS_CAMEO_EMBED`` (default ``OFF``) integrates the zero-heap, freestanding
C++20 **Cameo.Embed** bytecode virtual machine (``include/cameo_embed_vm.hpp``)
and NexRT adapter (``include/rtos_cameo_embed.hpp``) for hardware bring-up,
AXI peripheral diagnostics, and deterministic embedded scripting without
reflashing firmware.

Static Firmware Contract
------------------------

* **Zero heap**: no ``malloc``, ``free``, ``operator new``, or ``operator delete``.
  ``cameo::embed::StaticVm`` and ``cameo::embed::PackageReceiver`` store all
  stacks, frames, globals, fixed records, AXI scratchpad, and window descriptors
  in fixed-capacity ``.bss`` arrays.
* **Zero exceptions, zero RTTI, zero runtime constructors**: compiles cleanly
  under ``-std=c++20 -ffreestanding -fno-exceptions -fno-rtti -fno-builtin``
  and passes the ``Rtos.Tools`` forbidden-symbol and ``.init_array`` audit.
* **Integer-only by default** (``CAMEO_EMBED_ENABLE_FLOAT=0``): zero ``double``
  operations and zero ``libm`` / soft-float footprint on RV32IM and ARM Cortex-M3.
* **Compact 32-bit value mode** (``CAMEO_EMBED_COMPACT_32=1``): shrinks
  ``cameo::embed::Value`` from 16 bytes to 8 bytes (cutting stack/global/record
  SRAM usage by ~50% on 32-bit MCUs like Cortex-M3 and RV32IM; mutually
  exclusive with ``CAMEO_EMBED_ENABLE_FLOAT=1``).
* **No physical units, no dynamic hash dictionaries, no heap strings**: scripts
  are statically validated by ``cameo embed`` (``teal-cs/src/Cameo.Embed``) and
  sealed into 8-byte-aligned, Castagnoli CRC-32C (``0x82F63B78``) ``CEMB`` v1
  packages.
* **Formal proof & header parity**: ``formal/CameoEmbedProof.dfy`` models
  fail-closed ``CEMB`` verification, global read-only lockout, ``MemoryWindow``
  bounds/alignment soundness, bounded step termination, and ``step_one`` progress;
  ``teal-cs/embed/cameo_embed/run.sh`` verifies byte-for-byte parity between
  ``teal-cs`` and ``rtos`` headers when ``NEXRT_DIR`` is set.

Pointers, AXI Policy, Overlays, Watchpoints, and Debugging Surface
------------------------------------------------------------------

* **First-class pointers & struct/bitfield overlays (``Ptr``)**: ``ptr(addr)``,
  ``p + offset``, ``p - offset``, ``p.diff(q)``, ``p.align_up(align)``,
  ``p.is_aligned(align)``, ``p.read8/16/32/64()``, ``p.write8/16/32/64(val)``,
  and zero-overhead offset/bitfield overlays ``p.read8/16/32/64_at(off)``,
  ``p.write8/16/32/64_at(off, val)``, ``p.bits32_at(off, lsb, width)``, and
  ``p.rmw_bits32_at(off, lsb, width, field_val)`` (all writes rejected under
  ``--read-only``). ``cameo embed --gen-regmap <svd.xml|riscue|mps2>`` generates
  typed zero-metatable Cameo peripheral overlays.
* **Explicit AXI MMIO, watchpoints & bus trace (``axi.*``)**: ``read8/16/32/64``,
  ``write8/16/32/64``, ``rmw32``, ``poll32`` (step-budgeted and abortable),
  ``burst_read32`` / ``burst_write32`` via static scratchpad, ``fence``,
  ``window_check``, plus hardware watchpoints and static bus tracing
  (``axi.watch``, ``axi.unwatch``, ``axi.watch_hits``, ``axi.trace_enable``,
  ``axi.trace_count``). Every transaction is validated against the board's
  ``cameo::embed::MemoryWindow`` table (read-only vs. read-write permissions,
  min/max width for 32-bit AXI4-Lite slaves, and strict natural alignment).
  Watchpoint hits optionally halt execution (``WatchFlags::kBreak``) and
  forward event ``0x0101`` into ``rtos::trace::TraceRing``. Presets are provided
  in ``cameo::nexrt::AxiWindowPresets`` for RISCue UARTLite, ``RTOS_HWSCHED_BASE``,
  and MPS2 CMSDK Timer0/UART0.
* **Hardware debugging (``dbg.*``)**: ``bits``, ``set_bits``, ``bit``, ``popcount``,
  ``clz32``, ``ctz32``, ``bswap32``, ``log_hex``, ``hexdump`` (formatted in a
  fixed stack buffer into ``rtos::Logger`` or monitor output), ``assert``,
  ``expect_eq``, ``trace`` (wired to ``rtos::trace::TraceRing``), and ``breakpoint``.
* **RTOS, DMA & HWSched inspection (``rtos.*``)**: ``task_count``, ``task_state``,
  ``task_priority``, ``task_stack_bytes``, ``task_guard_ok``, ``task_cycles``,
  ``queue_depth``, ``dma_state``, ``dma_transferred``, ``dma_capacity``,
  ``dma_generation``, ``hwsched_status``, ``hwsched_ready_mask``,
  ``hwsched_fault_mask``, ``hwsched_seu_count``, and ``hwsched_dispatches``,
  alongside ``ticks``, ``sleep``, ``yield``, ``log``, ``watchdog_checkin``,
  ``queue_send``, ``queue_recv``, ``notify``, and GPIO.

Flash Slots, Live UART Hot-Loading, Stepping, and QEMU Cortex-M3
----------------------------------------------------------------

Compile a ``.cameo`` script with ``cameo embed`` (including a source-line debug
map sidecar and optional direct upload):

.. code-block:: bash

   cameo embed --boot-only-closures \
     --symbol kAxiDiagImage --namespace firmware::diag \
     --out-header axi_diag_image.hpp \
     --out-bin axi_diag.cemb \
     --out-hex axi_diag.hex \
     --out-map axi_diag.cemb.map.json \
     tests/scripts/axi_diag.cameo

Attach ``cameo::nexrt::MonitorBridge`` to ``rtos::monitor::Hooks::run_script``
(and optionally bind ``cameo::nexrt::JournalFsScriptStore`` via ``attach_storage``
for power-fail-atomic ``RTOS_STORAGE`` flash script slots):

.. code-block:: text

   script reset
   script hex 43454D42...
   script load
   script save 0
   script boot 0
   script erase 0
   script break <proto> <pc>
   script clearbp
   script start diagnose_bus 9
   script step
   script regs
   script cont
   script bustrace [clear]
   script run diagnose_bus 9
   script status

Build and run the live QEMU ARM Cortex-M3 (``mps2-an385``) firmware demo
(``examples/cameo_embed_arm.cpp``, ``CAMEO_EMBED_COMPACT_32=1``, passing the
``Rtos.Tools`` zero-heap / zero-``.init_array`` audit) with live UART hot-load,
single-stepping, watchpoint, and bus-trace verification via ``NexRT.ArmSim``:

.. code-block:: bash

   cmake --preset arm-cameo-embed
   cmake --build build/arm-cameo-embed --target demo
   cmake --build build/arm-cameo-embed --target arm_cameo_embed

