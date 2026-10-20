Static memory and lifetime rules
================================

Allocation policy
-----------------

Every kernel resource is reserved before scheduling: the thread pointer table,
application control blocks and stacks, private idle control block/stack,
and optional fixed-size Ethernet adapter buffers. Registration only records
caller-supplied storage. No kernel path calls malloc, free, new, or delete.
The boot registry closes at start, and finished thread slots remain reserved.
Thus kernel resource usage cannot grow as threads sleep, switch, or finish.

The post-link audit rejects recognized allocator symbols and dynamic C++
initializer functions in the final demo. This is evidence about the linked
image, not a universal memory-safety proof. Application/third-party allocators,
custom allocation functions, bad pointers, buffer overruns, and lost external
buffer ownership still require review and testing. Run the same audit on each
final application image and inspect its map.

The linker additionally retains and rejects nonempty constructor/preinitializer
arrays. This prevents linker garbage collection from silently removing required
runtime constructors. The ``static_contract_check`` build target exercises a
constant-initialized image, an unsupported constructor, and an allocator symbol.

Thread objects must not be placed inside stacks. Registered stack ranges must
not overlap each other or registered control blocks. Registration checks those
relationships, but cannot discover aliasing with arbitrary application buffers.
No automatic storage object from a returning boot helper may back a thread.
Names and arguments are borrowed; keep their storage alive too.

Stack sizing
------------

A thread stack holds application calls, a 144-byte trap frame, and C++ kernel
trap calls including any compiler arithmetic helpers. 512 bytes is an accepted
minimum, not a workload sizing recommendation. Begin with at least 1024 bytes,
inspect generated ``.su`` stack-usage files, analyze worst-case call depth,
and test maximum interrupt timing and application paths. Recursion and large
automatic arrays complicate bounded analysis.

The optional guard writes a sentinel into the lowest stack word and checks it
at trap scheduling boundaries and in diagnostics. A corrupt sentinel halts
when detected. A large write or stack jump can miss the sentinel; corruption
may already have occurred before any trap. No MPU/MMU isolation or high-water
scanning is provided. Guards do not make undersized stacks safe.

Memory map
----------

The default linker script uses RAM from zero through ``0x7eff``, keeps firmware
below the RISCue mailbox at ``0x7f00``, and reserves 4 KiB at the top of this
region for boot stack use by default. ``RTOS_BOOT_STACK_BYTES`` selects a
16-byte-aligned reserve from 512 through 32512 bytes; the linker also rejects
any overlap with the image. The lower bound is a configuration constraint,
not a qualified safe stack size. Application/idle stacks occupy BSS within the linked
image; they are separate from that boot reserve. A link-time assertion rejects
images that consume the reserve. This single RAM image has an RWX load segment;
there is no memory-permission isolation on this machine-mode port.

Static storage budget is::

   kernel globals + thread registry
   + application Thread objects and Stack objects
   + idle control block and stack
   + enabled driver rings/buffers and protocol storage
   + .data/.rodata/.text + boot stack reserve

Use sizeof(Thread) from the exact build, since metrics alter its layout.
The example does not link a network stack or reserve DMA rings.

Reducing the boot reserve
-------------------------

The default remains 4096 bytes. Configure, for example,
``-DRTOS_BOOT_STACK_BYTES=1024`` only after analyzing your startup path.
The native footprint tool reads ``_stack_top`` and ``_boot_stack_bottom`` from
the unstripped ELF, so a custom reserve is counted rather than assumed.
Missing symbols fail measurement instead of silently reporting 4096 bytes.

The current nano compiler reports 16 bytes for ``main``, 48 for ``create``,
32 for ``Scheduler::conflicts``, 48 for ``start`` and 32 for ``rtos_trap``.
These are individual function frames, not a complete bound. The first dispatch
also places a 144-byte assembly trap frame on the boot stack while main/start
remain active. Inspect disassembly, compiler helper calls and all boot-time
application/driver initialization before choosing a smaller reserve. Validate
on hardware with a maximum-use workload; future compiler/settings changes
require renewed analysis. No 1024-byte stack qualification is implied.

The 19 October 2026 cross-link comparison used the same nano sources, RV32
compiler and options with only this reserve changed. Code/constants remained
3812 bytes and BSS remained 1164 bytes. A 4096-byte reserve committed 9072
bytes of RAM; a 1024-byte reserve committed 6000 bytes. Both final images
passed the static-only allocation audit. The reduced-reserve ELF identity
and section totals are retained in ``site/footprint-small-boot.json``;
the canonical footprint and HTML introduction continue to use the default
reserve. These are linked capacity measurements, not observed stack maxima.

Runtime traps use the interrupted thread stack. A smaller boot reserve cannot
replace adequate per-thread stack sizing. Optional ground-linked diagnostics
and QSPI boot/XIP are described in :doc:`diagnostic-overlays`.

Driver ownership
----------------

Each buffer must have one owner at a time. For Ethernet use explicit transitions:
free descriptor to hardware-owned RX, completed RX to driver, copied adapter
frame to stack callback, then release/recycle. The adapter owns one permanent
1536-byte RX scratch buffer. Its input pointer is borrowed only during callback.
Transmit must consume or copy bytes synchronously before returning. DMA drivers
must use their own fixed rings and may return busy instead of retaining the
adapter caller's pointer. Bound all pool exhaustion behavior and count drops.
