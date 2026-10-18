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
region for boot stack use. Application/idle stacks occupy BSS within the linked
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

Driver ownership
----------------

Each buffer must have one owner at a time. For Ethernet use explicit transitions:
free descriptor to hardware-owned RX, completed RX to driver, copied adapter
frame to stack callback, then release/recycle. The adapter owns one permanent
1536-byte RX scratch buffer. Its input pointer is borrowed only during callback.
Transmit must consume or copy bytes synchronously before returning. DMA drivers
must use their own fixed rings and may return busy instead of retaining the
adapter caller's pointer. Bound all pool exhaustion behavior and count drops.
