Separate Bootloader Specification
=================================

NexRT is designed to boot from a **separate, immutable, write-protected
bootloader** residing in internal ROM, boot flash, or a locked flash sector.
The bootloader never links the RTOS scheduler, and the RTOS never modifies
bootloader sectors.

Bootloader Responsibilities
---------------------------

1. **Hardware reset & deterministic clock/memory init:** Initialize SRAM/TCM,
   clock tree/PLL, external flash controller (SPI NOR / QSPI / Parallel NOR /
   ONFI NAND), and the hardware watchdog before any RTOS code runs.
2. **Image slot selection & boot-loop Safe Mode:** Inspect the CRC-32C-sealed
   ``BootHandoff`` record in retained SRAM (linker-reserved ``.noinit.handoff``
   section). Maintain ``boot_attempts`` across warm resets:

   * On Cold Power-On Reset (POR) or invalid CRC-32C: initialize ``BootHandoff``
     with ``boot_reason = power_on``, ``active_slot = flight_a``,
     ``boot_attempts = 1``, ``safe_mode = false``.
   * On Watchdog / Fault / Brownout reset: increment ``boot_attempts``. Once
     ``boot_attempts > max_boot_attempts`` (default ``3``), automatically latch
     ``safe_mode = true`` and select the write-protected ``golden`` image slot.
   * Only the running RTOS ``HealthManager::confirm_boot_healthy()`` clears
     ``boot_attempts = 0`` after critical threads pass startup ``Barrier`` and
     self-tests.

3. **Image validation & authentication:** Every bootable slot carries a
   fixed 64-byte portable ``ImageHeader`` (magic ``"NXIM"``, version, target ID,
   load address, entry address, image byte length, BSS zero-fill length, build
   ID, CRC-32C, and optional 32-byte SHA-256 / mission signature digest). Any
   corrupt or out-of-range header fails closed and falls back to the next valid
   slot (``flight_a`` → ``flight_b`` → ``golden``).
4. **Copy boot vs. XIP:**

   * **Copy boot (RISCue flat-RAM, SRAM MCUs):** Copy validated payload from
     Serial NOR / Parallel NOR / ``JournalFs`` on NAND into target RAM, zero
     ``bss_bytes``, flush D-cache and invalidate I-cache, then jump to ``entry``.
   * **Execute-in-Place (XIP NOR):** Copy ``.data`` to RAM, zero ``.bss``,
     verify flash wait-states, and branch directly to ``entry`` in mapped NOR.

5. **Clean handoff state:** Mask all peripheral interrupts, set stack pointer to
   ``_stack_top``, seal ``BootHandoff`` with Castagnoli CRC-32C, and jump to
   the RTOS reset entry with ``BootHandoff*`` at the board-defined fixed address.

Retained-SRAM ``BootHandoff`` Binary ABI
----------------------------------------

Defined in ``include/rtos_boot.hpp`` (exact 48 bytes, zero implicit padding,
little-endian):

.. list-table:: BootHandoff wire layout (48 bytes)
   :header-rows: 1
   :widths: 16 14 14 56

   * - Offset
     - Type
     - Field
     - Meaning
   * - ``0x00``
     - ``uint32_t``
     - ``magic``
     - Fixed ``0x4E584248`` (``"NXBH"``)
   * - ``0x04``
     - ``uint16_t``
     - ``version``
     - Protocol version (``1``)
   * - ``0x06``
     - ``uint8_t``
     - ``boot_reason``
     - ``0`` = POR, ``1`` = WDT, ``2`` = Fault, ``3`` = Ground Cmd, ``4`` = Brownout
   * - ``0x07``
     - ``uint8_t``
     - ``active_slot``
     - ``0`` = Golden, ``1`` = Flight A, ``2`` = Flight B
   * - ``0x08``
     - ``uint16_t``
     - ``boot_attempts``
     - Consecutive unconfirmed boot attempts
   * - ``0x0A``
     - ``uint16_t``
     - ``max_boot_attempts``
     - Threshold before automatic Safe Mode (default ``3``)
   * - ``0x0C``
     - ``uint32_t``
     - ``flags``
     - Bit 0: ``safe_mode``; Bit 1: ``confirmed_healthy``; Bit 2: ``crash_logged``
   * - ``0x10``
     - ``uint32_t``
     - ``image_build_id``
     - Running firmware build identifier
   * - ``0x14``
     - ``uint32_t``
     - ``boot_count``
     - Monotonic lifetime warm/cold boot counter
   * - ``0x18``
     - ``uint32_t``
     - ``last_fault_pc``
     - Captured program counter on fault/watchdog reset
   * - ``0x1C``
     - ``uint32_t``
     - ``last_fault_code``
     - Fault / watchdog reason code
   * - ``0x20``
     - ``uint64_t``
     - ``total_uptime_ticks``
     - Accumulated ticks prior to warm reset
   * - ``0x28``
     - ``uint32_t``
     - ``reserved``
     - Zero-filled explicit alignment word
   * - ``0x2C``
     - ``uint32_t``
     - ``crc32``
     - Castagnoli CRC-32C over bytes ``0x00..0x2B``

Flight vs. Bench Build Configuration
------------------------------------

All optional modules (``RTOS_MONITOR``, ``RTOS_SCRIPT``, ``RTOS_ELF_LOADER``)
default to ``OFF`` so minimal and flight presets exclude them unless explicitly
enabled. The choice remains entirely with the developer:

* Setting ``RTOS_FLIGHT_BUILD=ON`` tags the image as a flight configuration in
  the CMake **NexRT Build Manifest** banner without blocking any optional
  module.
* If the developer explicitly enables ``RTOS_MONITOR=ON``, ``RTOS_SCRIPT=ON``,
  or ``RTOS_ELF_LOADER=ON`` alongside ``RTOS_FLIGHT_BUILD=ON``, the configure
  manifest surfaces them clearly as ``[FLIGHT + OPT-IN: ...]`` so release
  reviews have exact visibility into every linked module.

* Every configure prints an explicit **NexRT Build Configuration Manifest**
  listing enabled modules and the flight-safety verdict.
