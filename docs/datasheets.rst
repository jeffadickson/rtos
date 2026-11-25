Supported Targets, ARM Variants, and Component Datasheets
=========================================================

This reference lists every processor target and ARM architecture variant tracked
by NexRT alongside its honest qualification status, plus the exact flash memory
families, protocols, geometries, and verified vendor datasheets supported by
``RTOS_STORAGE`` (``include/rtos_storage.hpp`` / ``src/storage.cpp``).

Supported Targets & ARM Variants Matrix
---------------------------------------

NexRT maintains strict honesty about hardware qualification. Only targets with
an implemented architecture port and executable verification (cross-build +
static ELF audit, RTL simulation, or QEMU execution) are marked ready; planned
and provisional ARM/FPGA targets deliberately fail firmware generation until
their exact startup, interrupt controller, and memory-map BSPs are reviewed.

.. list-table:: Processor targets and ARM architecture variants
   :header-rows: 1
   :widths: 17 14 14 14 16 25

   * - Target ID
     - CPU / Core
     - ISA / ARM Profile
     - HW Protection & Co-Design
     - Qualification Status
     - Architecture Manual / Vendor Datasheet
   * - ``riscue``
     - RISCue soft core
     - RISC-V ``RV32IM`` + ``Zicsr`` (M-mode)
     - PMP optional; ``RTOS_HW_SCHED`` + ``RTOS_RISCUE_BANKED`` (8-bank TMR RF)
     - **Cross-Built + RTL Verified** (PLAIN UART + Verilator + Dafny lockstep; board qual pending)
     - `RISC-V Unprivileged / Privileged ISA Specs <https://riscv.org/specifications/>`_
   * - ``tang-nano-9k-riscue``
     - RISCue on Gowin GW1NR-9 FPGA
     - RISC-V ``RV32IM`` soft SoC
     - Depends on synthesized bitstream & PLL
     - **Provisional** (FPGA board; requires bitstream memory map)
     - `Sipeed Tang Nano 9K Wiki <https://wiki.sipeed.com/hardware/en/tang/Tang-Nano-9K/Nano-9K>`_
   * - ``qemu-mps2-m3``
     - ARM Cortex-M3 (QEMU MPS2 AN385)
     - **ARMv7-M** (Thumb-2, integer PSP/MSP)
     - 8-region **ARMv7-M MPU** stack guard (``RTOS_HW_PROTECT=ON``) + WCET watchdog
     - **Verified in QEMU Simulation** (SVC/SysTick/BASEPRI, MPU fault containment, stress suite)
     - `ARMv7-M Architecture Reference Manual (DDI0403E.e) <https://support.arm.com/documentation/ddi0403/ee/>`_; `QEMU MPS2 Docs <https://www.qemu.org/docs/master/system/arm/mps2.html>`_
   * - ``vorago-va108xx``
     - VORAGO VA10800 / VA10820 (Cortex-M0)
     - **ARMv6-M** (Thumb-1/subset Thumb-2, rad-hard >300 krad)
     - Software stack guard + EDAC SRAM (no ARMv6-M MPU)
     - **Provisional** (requires PendSV/SVC, NVIC, exact VA108xx memory map & BSP)
     - `VORAGO VA10820 Datasheet (PDF) <https://www.mouser.com/pdfdocs/VA10820_DS_12.pdf>`_; `VA10820 Product Page <https://www.voragotech.com/products/microcontrollers/arm-cortexm0-family/va10820>`_
   * - ``vorago-va416xx``
     - VORAGO VA41620 / VA41630 (Cortex-M4F)
     - **ARMv7E-M** (DSP + optional FPv4-SP, rad-hard >200–300 krad)
     - ARMv7E-M MPU + on-chip SRAM EDAC & scrubbing
     - **Provisional** (extends ARMv7-M port with FPU lazy-stacking ABI & VA416x0 BSP)
     - `VORAGO VA41620 Product Page <https://www.voragotech.com/products/microcontrollers/arm-cortex-m4-family/va41620>`_; `VORAGO VA41630 Product Page <https://www.voragotech.com/products/microcontrollers/arm-cortex-m4-family/va41630>`_
   * - ``zynq7000-a9``
     - AMD/Xilinx Zynq-7000 (Cortex-A9)
     - **ARMv7-A** (1 core 7000S / 2 cores 7010–7100)
     - MMU + L1/L2 caches + GICv1
     - **Planned** (needs ARMv7-A IRQ/SVC vectors, GIC, MMU/cache policy)
     - `AMD Zynq-7000 SoC Documentation <https://www.amd.com/en/products/adaptive-socs-and-fpgas/soc/zynq-7000.html>`_
   * - ``zynqmp-r5``
     - AMD/Xilinx Zynq UltraScale+ RPU (Cortex-R5F)
     - **ARMv7-R** (dual-core split or deterministic lockstep)
     - PMSAv7 MPU + ECC TCM + GIC-400
     - **Planned** (ideal aerospace real-time target; lockstep = 1 logical UP core)
     - `AMD Zynq UltraScale+ MPSoC Docs <https://www.amd.com/en/products/adaptive-socs-and-fpgas/soc/zynq-ultrascale-plus-mpsoc.html>`_
   * - ``zynqmp-a53``
     - AMD/Xilinx Zynq UltraScale+ APU (Cortex-A53)
     - **ARMv8-A** (AArch64, 2–4 cores)
     - EL1/EL3 MMU + GIC-400 + Generic Timer
     - **Planned** (requires 64-bit pointer-neutral context ABI)
     - `AMD Zynq UltraScale+ MPSoC Docs <https://www.amd.com/en/products/adaptive-socs-and-fpgas/soc/zynq-ultrascale-plus-mpsoc.html>`_
   * - ``microblaze``
     - AMD/Xilinx MicroBlaze 32-bit
     - Classic 32-bit MicroBlaze soft ISA
     - Optional MMU/MPU + AXI INTC/Timer
     - **Planned** (separate ISA/ABI; not MicroBlaze V RISC-V)
     - `AMD MicroBlaze Reference (UG984) <https://docs.amd.com/r/2024.1-English/ug984-vivado-microblaze-ref/Data-Types-and-Endianness>`_

.. note::
   **ARM Variant Architectural Differences in NexRT:**

   * **ARMv6-M (Cortex-M0 / VA108xx):** Uses ``PRIMASK`` (no ``BASEPRI``), lacks
     an MPU, and restricts ``LDMIA/STMIA`` to low registers ``r0-r7`` (requiring
     manual staging of ``r8-r11`` in ``context.S``).
   * **ARMv7-M (Cortex-M3 / MPS2-AN385 — Implemented):** Uses ``BASEPRI`` for
     bounded interrupt masking, full ``r4-r11`` ``STMDB/LDMIA`` context saves on
     PSP, and 8-region hardware MPU stack guards (``ports/arm-mps2/``).
   * **ARMv7E-M (Cortex-M4F / VA416xx):** Extends ARMv7-M with DSP instructions
     and optional FPv4-SP (``s16-s31`` stacking when ``EXC_RETURN[4] == 0``).
   * **ARMv7-R (Cortex-R5F) & ARMv7-A (Cortex-A9):** Replace NVIC/PendSV with
     GICv1/v2 + FIQ/IRQ banked modes and explicit cache/TCM coherency.
   * **ARMv8-A (Cortex-A53 AArch64):** Requires a 64-bit context frame
     (``x0-x30``, ``SP_EL0``, ``ELR_EL1``, ``SPSR_EL1``).

Supported Flash Parts & Vendor Datasheets (``RTOS_STORAGE``)
------------------------------------------------------------

``RTOS_STORAGE`` provides a single boot-static, zero-heap, power-fail-safe
journaling filesystem (``rtos::storage::JournalFs<MaxBlocks, MaxFiles, PageBytes>``)
layered over a unified ``FlashBackend`` HAL with four built-in protocol adapters.
Every file write appends a CRC-32C-sealed page (no in-place overwrites), making
the same filesystem natively safe across NOR and NAND technologies.

.. list-table:: Supported flash devices, adapters, geometry, and vendor datasheets
   :header-rows: 1
   :widths: 15 16 14 17 15 23

   * - Flash Family & Adapter
     - Reference Parts
     - Bus & Opcodes
     - Geometry (Page / OOB / Block)
     - ECC & Endurance
     - Official Vendor Datasheet
   * - **Serial SPI/QSPI NOR** (``SerialNorAdapter``)
     - **Winbond W25Q128JV** (also MX25L12835F, S25FL128L)
     - SPI Mode 0/3; ``0x9F`` ID, ``0x06`` WREN, ``0x05`` RDSR, ``0x03`` Read, ``0x02`` PP, ``0x20``/``0xD8`` Erase
     - 128 Mbit (16 MiB); 256 B prog chunk; 4 KiB sector / 64 KiB block; 0 B OOB
     - Native NOR (no ECC required); 100,000 P/E cycles, 20-year retention
     - `Winbond W25Q128JV Datasheet (PDF) <https://www.mouser.com/datasheet/2/949/w25q128jv_revf_03272018_plus-1489608.pdf>`_; `Winbond Product Page <https://www.winbond.com/hq/new-online-purchasing-guide/index.html?__locale=en&pLine=/product/code-storage-flash/qspi-nor/&pNo=W25Q128JV>`_
   * - **Serial SPI SLC NAND** (``SerialNandAdapter``)
     - **Winbond W25N01GV** (also Micron MT29F1G01ABAFD)
     - SPI; ``0x13`` Page Read to Cache, ``0x0F`` Get Feature, ``0x03`` Read Cache, ``0x02`` Load, ``0x10`` Prog Exec, ``0xD8`` Block Erase
     - 1 Gbit (128 MiB); 2048 B + 64 B OOB page; 64 pages/block (128 KiB); 1024 blocks
     - **On-die 1-bit ECC** (status bits ``[5:4]``); factory bad-block byte ``OOB[0] != 0xFF``; 100,000 P/E cycles
     - `Winbond W25N01GV Datasheet (PDF) <https://download.mikroe.com/documents/datasheets/W25N01GVZEIGIT_datasheet.pdf>`_
   * - **Parallel ONFI 1.0 x8 SLC NAND** (``ParallelNandAdapter``)
     - **Micron MT29F4G08ABA** (also Kioxia TC58NVG2S0H)
     - 8-bit CLE/ALE/R/B#; ``0x00/0x30`` Read, ``0x80/0x10`` Prog, ``0x60/0xD0`` Erase, ``0x70`` Status
     - 4 Gbit (512 MiB); 2048 B + 64 B OOB page; 64 pages/block (128 KiB); 4096 blocks
     - **SW 22-bit Hamming SECDED** (3 B ECC per 256 B chunk = 24 B in OOB ``[2..25]``); bad-block markers in ``OOB[0..1]``; 100,000 P/E cycles
     - `Micron MT29F4G08 Datasheet (PDF) <https://www.farnell.com/datasheets/3151368.pdf>`_
   * - **Parallel 16-bit CFI/JEDEC NOR** (``ParallelNorAdapter``)
     - **Infineon S29GL128S** (also Micron MT28EW128ABA)
     - 16-bit async bus; unlock ``0x555/0xAA``, ``0x2AA/0x55``; ``0xA0`` Word Prog, ``0x80/0x30`` Sector Erase, DQ7 poll
     - 128 Mbit (16 MiB); 128 KiB uniform sectors; 0 B OOB; XIP-capable
     - Native MirrorBit NOR + internal ECC; 100,000 P/E cycles
     - `Infineon S29GL128S / GL-S Datasheet (PDF) <https://www.infineon.com/assets/row/public/documents/10/49/infineon-s29gl01gs-s29gl512s-s29gl256s-s29gl128s-128-mb-256-mb-512-mb-1-gb-gl-s-mirrorbit-flash-parallel-3-datasheet-en.pdf>`_

Storage Architecture, Wear Leveling, and Formal Proof
-----------------------------------------------------

* **Append-only CRC-32C journal:** Each page starts with a 40-byte portable
  ``RecordHeader`` (``magic = "NXFS"``, version, record type, file ID, payload
  length, monotonic 64-bit ``sequence``, block ``erase_count``, payload CRC-32C,
  and header CRC-32C). Sudden power loss mid-write leaves a torn record whose
  CRC fails and is deterministically ignored on ``mount()``.
* **22-bit Complementary Hamming SECDED (Parallel NAND):** Each 256-byte chunk
  stores 3 bytes of complementary even/odd parity in the NAND OOB spare area
  starting at offset 2 (bytes 0–1 are reserved for ONFI factory bad-block
  markers). Single-bit data flips are silently corrected and flag the block for
  proactive scrubbing; double-bit errors are reliably detected and quarantined.
* **Dynamic + static wear leveling:** Free blocks are allocated strictly by
  lowest ``erase_count``. When ``max_erase_count - min_erase_count`` exceeds
  the configured ``wear_threshold``, ``gc_step()`` proactively relocates the
  coldest full block so static data rotates off low-wear blocks.
* **Read-disturb & ECC scrubbing:** Blocks experiencing a corrected 1-bit ECC
  event or reaching ``read_disturb_limit`` are prioritized by ``gc_step()`` and
  relocated before errors accumulate.
* **Permanent bad-block quarantine & ECC/EOL retirement:** Factory bad blocks
  (checked across page 0, page 1, and last page OOB markers, never erased),
  runtime program/erase failures (evacuating any live sibling files first),
  2-bit uncorrectable ECC faults, recurring 1-bit ECC degradation crossing
  ``ecc_retire_threshold``, and blocks reaching ``max_erase_cycles`` are
  permanently marked ``BlockState::bad`` and persisted via ``RecordType::bad_block``
  records that GC automatically relocates across reboots.
* **Empty-block wear persistence:** ``gc_step()`` writes a compact
  ``RecordType::erase_marker`` on recycled blocks so empty free blocks retain
  their exact ``erase_count`` across reboots.
* **Formal proof (``formal/StorageProof.dfy``):** Verified by Dafny + Z3
  (19/19 proof obligations) proving torn-write atomicity, ``EraseMarker``
  invariance, write isolation, GC live-file preservation, minimum-wear free
  allocation, coldest-victim selection, and permanent ECC/EOL bad-block exclusion.

Hardware-Free Flash Emulation (``include/rtos_flash_mocks.hpp``)
----------------------------------------------------------------

Every supported flash family ships with a **boot-static, zero-heap, freestanding
(``-nostdlib``) chip emulator** in ``include/rtos_flash_mocks.hpp``
(``rtos::storage::mocks``) so applications, QEMU Cortex-M3 / RISCue simulations,
and host tests can emulate real flash chips without physical hardware:

* ``SerialNorMock<Blocks, PagesPerBlock, PageBytes>`` — W25Q128JV / MX25L12835F / S25FL128L (SPI opcodes, configurable JEDEC ID, WIP busy polling, 1→0 AND physics).
* ``SerialNandMock<Blocks, PagesPerBlock, PageBytes, SpareBytes>`` — W25N01GV / MT29F1G01ABAFD (cache read/load, OIP busy polling, on-die ECC status injection, P_FAIL/E_FAIL, factory bad blocks).
* ``ParallelNandMock<Blocks, PagesPerBlock, PageBytes, SpareBytes>`` — ONFI 1.0 x8 MT29F4G08ABA / TC58NVG2S0H (CLE/ALE/R/B# timing, raw data/spare bit-flip injection for 22-bit Hamming SECDED testing).
* ``ParallelNorMock<Blocks, PagesPerBlock, PageBytes>`` — 16-bit CFI/JEDEC S29GL128S / MT28EW128ABA (unlock sequences, CFI ``QRY`` query, true DQ7 complement busy polling).

All four emulators expose a shared ``FaultConfig`` (``busy_polls``,
``fail_program_block``, ``fail_erase_block``, ``power_cut_after_ops``) and
``ChipTelemetry`` (``read_ops``, ``program_ops``, ``erase_ops``,
``busy_polls_served``, ``torn_cuts``, and per-block ``block_erase_count()``).

Synthesizable AXI4-Lite Verilog Flash Controllers (``rtl/``)
------------------------------------------------------------

To connect ``RTOS_STORAGE`` directly to FPGA SoCs (RISCue, Zynq-7000/ZynqMP PL,
MicroBlaze), NexRT includes synthesizable SystemVerilog AXI4-Lite flash
controllers verified end-to-end under Verilator (``flash_axi_verilator``):

* ``rtl/nexrt_axi_lite_if.sv`` & ``rtl/nexrt_axi_lite_slave.sv``: reusable
  SystemVerilog AXI4-Lite ``interface`` (``master``/``slave`` modports) and
  32-bit word-aligned register bridge with ``OKAY``/``SLVERR`` decoding.
* ``rtl/nexrt_spi_flash_axi.sv`` (``BASE + 0x0000``): SPI Mode 0/3 master with
  programmable clock divider, manual CS framing, and multi-chip-select support
  (``CS0#`` = Serial SPI NOR, ``CS1#`` = Serial SPI SLC NAND).
* ``rtl/nexrt_onfi_nand_axi.sv`` (``BASE + 0x1000``): Parallel ONFI 1.0 x8 NAND
  controller (CLE/ALE/WE#/RE#/CE#/WP#/split DQ/2-FF synchronized R/B#) with
  programmable setup/pulse/hold cycles and an inline **hardware 22-bit
  complementary Hamming-256 SECDED ECC engine** bit-exact with
  ``rtos::storage::hamming256_encode()``.
* ``rtl/nexrt_cfi_nor_axi.sv`` (``BASE + 0x2000``): Parallel 16-bit CFI/JEDEC
  NOR controller (CE#/OE#/WE#/RESET#/ADDR/split DQ[15:0]/RY_BY#) with
  programmable asynchronous wait states.
* ``rtl/nexrt_flash_subsys_axi.sv`` & ``rtl/nexrt_flash_subsys_axi_if.sv``:
  unified 16 KiB AXI4-Lite multi-flash wrapper with ``ENABLE_*`` synthesis
  parameters, subsystem capability/ID registers (``BASE + 0x3000``), and
  aggregated ``flash_irq`` output.
* ``include/rtos_storage_axi.hpp``: freestanding C++20 AXI4-Lite transport
  drivers (``AxiSpiMaster``, ``AxiOnfiNand``, ``AxiCfiNor``) binding the RTL
  register maps directly to ``SerialNorAdapter``, ``SerialNandAdapter``,
  ``ParallelNandAdapter``, and ``ParallelNorAdapter``.


