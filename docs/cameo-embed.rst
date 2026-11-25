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
* **No physical units, no dynamic hash dictionaries, no heap strings**: scripts
  are statically validated by ``cameo embed`` (``teal-cs/src/Cameo.Embed``) and
  sealed into 8-byte-aligned, Castagnoli CRC-32C (``0x82F63B78``) ``CEMB`` v1
  packages.

Pointers, AXI Policy, and Debugging Surface
-------------------------------------------

* **First-class pointers (``Ptr``)**: ``ptr(addr)``, ``p + offset``, ``p - offset``,
  ``p.diff(q)``, ``p.align_up(align)``, ``p.is_aligned(align)``, ``p.read8/16/32/64()``,
  and ``p.write8/16/32/64(val)``.
* **Explicit AXI MMIO (``axi.*``)**: ``read8/16/32/64``, ``write8/16/32/64``,
  ``rmw32``, ``poll32`` (step-budgeted and abortable), ``burst_read32`` /
  ``burst_write32`` via static scratchpad, ``fence``, and ``window_check``.
  Every transaction is validated against the board's explicit
  ``cameo::embed::MemoryWindow`` table (read-only vs. read-write permissions,
  min/max width for 32-bit AXI4-Lite slaves, and strict natural alignment).
  Presets are provided in ``cameo::nexrt::AxiWindowPresets`` for RISCue UARTLite
  and ``RTOS_HWSCHED_BASE``.
* **Hardware debugging (``dbg.*``)**: ``bits``, ``set_bits``, ``bit``, ``popcount``,
  ``clz32``, ``ctz32``, ``bswap32``, ``log_hex``, ``hexdump`` (formatted in a
  fixed stack buffer into ``rtos::Logger`` or monitor output), ``assert``,
  ``expect_eq``, ``trace`` (wired to ``rtos::trace::TraceRing``), and ``breakpoint``.
* **RTOS inspection (``rtos.*``)**: ``task_count``, ``task_state``,
  ``task_priority``, ``task_stack_bytes``, ``task_guard_ok``, ``task_cycles``,
  and ``queue_depth``, alongside ``ticks``, ``sleep``, ``yield``, ``log``,
  ``watchdog_checkin``, ``queue_send``, ``queue_recv``, ``notify``, and GPIO.

Flash Headers and UART Monitor Hot-Loading
------------------------------------------

Compile a ``.cameo`` script with ``cameo embed``:

.. code-block:: bash

   cameo embed --boot-only-closures \
     --symbol kAxiDiagImage --namespace firmware::diag \
     --out-header axi_diag_image.hpp \
     --out-bin axi_diag.cemb \
     --out-hex axi_diag.hex \
     tests/scripts/axi_diag.cameo

Attach ``cameo::nexrt::MonitorBridge`` to ``rtos::monitor::Hooks::run_script``
to stream ``.cemb`` hex payloads over UART and run functions live:

.. code-block:: text

   script reset
   script hex 43454D42...
   script load
   script run diagnose_bus 9
   script status
