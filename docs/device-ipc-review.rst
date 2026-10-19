Device sharing and IPC: requirements review
===========================================

Status: planned design, not an implemented API. This pass adds requirements
NXR-113 through NXR-132 and records 100 distinct design decisions below.
No builds, tests, firmware measurements or commits were requested or performed
for this pass. Acceptance checks below are future verification obligations.
Existing static_storage::Ring is single-owner storage, not a concurrent queue.

Configuration and memory
------------------------

Proposed independent switches are RTOS_DEVICE, RTOS_QUEUE, RTOS_EVENTS,
RTOS_SEMAPHORE and RTOS_MUTEX_PI; names are design proposals until implemented.
All default off in the nano profile. Capacities and storage are fixed at boot.
No worker thread or request queue is mandatory for a simple register driver.
Report exact target sizeof values and linker contributions before publishing
memory claims. Eight 64-byte logging slots with four bytes of header cost
544 bytes before stronger ownership metadata, alignment or synchronization.
DMA is deferred: no descriptors, retained transfer buffers or cancellation API
are part of this phase. Third-party Ethernet DMA remains a separate BSP audit.

100 design iterations
---------------------

Device registration (NXR-113)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

001. Reserve device objects and the registry before scheduling; reject runtime registration.
002. Seal the registry only after initialization succeeds; failed devices stay unavailable.
003. Reject duplicate identity and duplicate exclusive MMIO ownership at boot.
004. Use stable handles into permanent storage; no destruction while threads can reference them.
005. Keep register-only drivers independent of queue, logger and worker-thread modules.

Driver operations (NXR-114)
~~~~~~~~~~~~~~~~~~~~~~~~~~~

006. Define initialize, read, write and typed control as a small documented operations contract.
007. Return unsupported for absent optional operations without touching hardware.
008. Separate argument rejection from hardware errors so callers can distinguish recovery actions.
009. Document callback context, maximum work and whether an operation may sleep.
010. Provide a minimal FPGA register-driver example with no heap or hidden background thread.

Transaction serialization (NXR-115)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

011. Protect the complete command/data/status transaction, not individual register accesses.
012. Serialize all thread-facing entry points through the same device transaction lock.
013. Keep the mutex held across completion waits when registers must remain reserved.
014. Release the transaction lock on every normal error and timeout return.
015. Expose concurrent-access restrictions for aliases and raw register escape hatches.

Priority inheritance (NXR-116)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

016. Store base and effective priority separately; donation must not overwrite application policy.
017. Propagate donation through bounded lock dependency chains.
018. Recompute donations after timeout, unlock and runtime base-priority changes.
019. Select highest effective-priority waiter; preserve FIFO order among equals.
020. Reject self-lock and dependency cycles before sleeping; cap traversal by registered threads.

MMIO contracts (NXR-117)
~~~~~~~~~~~~~~~~~~~~~~~~

021. Validate offset alignment and access width before issuing a bus operation.
022. Check offset plus width without arithmetic overflow against the declared register aperture.
023. Encode read-only, write-only and write-one-to-clear semantics in register definitions.
024. Use explicit port ordering hooks; volatile accesses alone do not define hardware ordering.
025. Document register endianness and side effects; never read back a destructive register implicitly.

Completion waiting (NXR-118)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

026. Atomically combine condition checking and waiter enrollment to prevent lost completion wakeups.
027. Recheck the device predicate after every wakeup before consuming a result.
028. Store completion state persistently so an interrupt before waiting is not lost.
029. Tag successive operations so a delayed completion cannot satisfy a newer transaction.
030. Use one absolute deadline across all retries instead of restarting the timeout.

ISR boundary (NXR-119)
~~~~~~~~~~~~~~~~~~~~~~

031. Provide explicitly named ISR-safe entry points; reject blocking calls from interrupt context.
032. Acknowledge device interrupts according to the BSP sequence before returning.
033. Bound ISR work and defer lengthy parsing or copying to thread context.
034. Request rescheduling at interrupt exit when a higher-priority waiter becomes ready.
035. Do not advertise peripheral IRQ support until external interrupt dispatch and nesting rules exist.

Device fault handling (NXR-120)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

036. Distinguish lock-wait timeout, responsive-device timeout and bus-access failure.
037. Require an FPGA interconnect error responder or independent timeout for stalled AXI transactions.
038. Mark a device faulted after an uncertain partial command; reject new commands until recovery.
039. Permit reset only while transaction ownership and interrupt quiescence are established.
040. Specify safe register state after recovery; a thread timeout alone does not prove quiescence.

Optional request worker (NXR-121)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

041. Make queued service opt-in for slow devices; direct locked access remains the small default.
042. Reserve request slots and worker stack at boot with explicit capacity limits.
043. Copy bounded request data or require a lifetime contract covering every retained pointer.
044. Define whether the worker serves FIFO or priority order and expose starvation implications.
045. Separate cancellation of queued work from an already-issued hardware command.

Typed queue storage (NXR-122)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

046. Use Queue<T,N> with positive compile-time capacity and fixed message storage.
047. Initially restrict messages to trivially copyable values; no hidden constructors or allocation.
048. Copy successful sends into kernel-owned queue storage before returning success.
049. Preserve FIFO message order independently of the priority used to select waiting threads.
050. Publish exact payload, alignment, queue-control and waiter storage budgets per target.

Blocking queue operations (NXR-123)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

051. NoWait returns full or empty immediately without registering a sleeper.
052. Timed send and receive sleep only when the requested transfer cannot complete.
053. Leave receive output unchanged on timeout, empty or invalid arguments.
054. Resolve timeout versus transfer under one serialization rule; return exactly one outcome.
055. Wake the highest-priority eligible waiter and transfer ownership before another contender steals it.

Queue ISR operations (NXR-124)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

056. try_send_from_isr and try_receive_from_isr never sleep or retry without a bound.
057. Queue publication and dequeue use the port's verified critical-section or atomic protocol.
058. Prevent nested interrupt producers from corrupting queue indices or reservation metadata.
059. Return full or empty explicitly; do not silently overwrite queued messages.
060. Define maximum message-copy work in interrupt context and allow a smaller ISR size cap.

Event flags (NXR-125)
~~~~~~~~~~~~~~~~~~~~~

061. Define a fixed-width flag word with a documented usable-bit mask.
062. Reject zero wait masks and unsupported bits before changing waiter state.
063. Support wait-any and wait-all with returned observed bits.
064. Define clear-on-success atomically; timeout never consumes event bits.
065. Treat repeated sets as coalesced conditions, not counted occurrences; use a semaphore for counts.

Event wake ordering (NXR-126)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

066. Define broadcast evaluation against a snapshot for non-clearing event waits.
067. For consuming waits, evaluate highest-priority first and recompute remaining bits after each grant.
068. Serialize set, clear, wait enrollment and timeout removal to avoid lost updates.
069. Provide nonblocking ISR set with deferred scheduling at the interrupt boundary.
070. Document that callers protect associated payload separately; a flag does not own a buffer.

Counting semaphore (NXR-127)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

071. Validate initial count against a boot-fixed maximum.
072. Saturation returns overflow without changing the count or silently wrapping.
073. A release grants a waiting thread before making a token available to new contenders.
074. Timeout and release races resolve once; removed waiters cannot receive stale tokens.
075. Semaphores have no owner and no priority inheritance; do not present them as mutex substitutes.

Wait lifecycle (NXR-128)
~~~~~~~~~~~~~~~~~~~~~~~~

076. Store at most one active blocking operation in each thread control block initially.
077. Remove a thread from wait structures exactly once on wake, timeout or supported termination.
078. Reject idle-thread blocking and blocking before scheduling starts.
079. Bound tick delays to the supported modular-time comparison range.
080. Keep all queue and waiter links private so application mutation cannot corrupt scheduler lists.

Owner exit (NXR-129)
~~~~~~~~~~~~~~~~~~~~

081. Do not silently unlock a device mutex whose owner exits during a partial transaction.
082. Mark abandoned ownership as owner-dead and keep the device unavailable until explicit recovery.
083. Wake waiters with a distinct failure result rather than falsely successful lock ownership.
084. Remove donations from dead owners and cancelled waiters without dangling thread references.
085. Document that fatal kernel faults and memory corruption are outside ordinary owner-exit recovery.

Metrics (NXR-130)
~~~~~~~~~~~~~~~~~

086. Snapshot mutex owner, effective priority and waiter count coherently into caller storage.
087. Expose queue depth, capacity, high-water mark and failed nonblocking operations.
088. Count timeout, hardware error and recovery separately instead of conflating all failures.
089. Saturate counters under the same synchronization discipline used for their objects.
090. Bound snapshot work and support count-only discovery without allocating output arrays.

Configuration and portability (NXR-131)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

091. Disable each IPC/device module independently and remove its storage in the nano profile.
092. Validate feature dependencies before compilation, especially device-lock priority inheritance.
093. Make the current single-hart restriction explicit; IRQ masking is not an SMP lock.
094. Preserve a future port synchronization seam without claiming unverified multicore safety.
095. Keep CMake/Ninja firmware configuration and C# XDoc host tooling as existing project policy.

Logging and documentation (NXR-132)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

096. Keep logging optional with boot-fixed depth and message size; no mandatory 256-slot queue.
097. Define drop-new as the initial policy and account for truncation separately from queue-full loss.
098. Drain only with a bounded byte/work budget and retain bytes until synchronous consumption completes.
099. Require context-safe private formatting; ordinary thread-local buffers are not automatically nested-ISR safe.
100. Label proposed APIs and future acceptance scenarios clearly; no build or runtime proof follows from review.

Historical comparison: XENIX
----------------------------

Intel's 1984 XENIX 286 Device Driver Guide, sections 2-6 through 2-8,
describes a process-side driver sleeping on an address and an interrupt-side
routine waking all processes waiting on that address. The condition is checked
again after waking. Interrupt-priority controls protect short shared-state
updates. Its kernel did not permit another process to preempt an executing
kernel process until it slept or left the kernel. The manual's terminal-driver
example also separates initialization, character queues and interrupt service.

The useful inheritance is the split between thread-side requests and short
interrupt-side completion handling. Our preemptive kernel additionally needs
atomic waiter enrollment, explicit mutex ownership and priority inheritance.
Address-based wakeup is not the same contract as a typed message queue, and
historical process IPC must not be represented as this proposed thread API.

Primary source: `Intel XENIX 286 Device Driver Guide, November 1984
<https://www.bitsavers.org/pdf/intel/system3xx/xenix-286/174393-001_XENIX_286_Device_Driver_Guide_Nov84.pdf>`_.

Review evidence and limits
--------------------------

Inspected include/rtos.hpp, src/scheduler.hpp, include/rtos_static.hpp,
docs/drivers.rst and the canonical requirements catalogue. The current public
Thread state has no IPC-blocked state; static Ring explicitly requires external
synchronization; external peripheral interrupt dispatch remains unfinished.
All new requirements therefore remain Planned. Existing related requirements
remain in force; this batch refines their contracts rather than proving them.
These are 100 focused decisions in one design-review pass, not 100 independent
expert reviews or executed implementation cycles.

Optional DMA and possible SpaceVPX PCIe roadmap
-----------------------------------------------

This later roadmap refines the earlier DMA deferral: DMA remains outside the
initial device/IPC implementation but is now explicitly planned as an optional
BSP driver after interrupt dispatch, waits and mutexes are ready. It is not a
mandatory kernel service. Proposed configuration switches are not implemented.

Phase 1: implement shared register devices and IPC (NXR-113 through NXR-132).
Phase 2: select one actual FPGA DMA engine and implement one active transfer per
channel, static aligned buffers, thread serialization and interrupt completion.
Reuse a suitable vendor standalone driver behind the ownership contract.
Initially exclude scatter/gather, cyclic DMA and automatic owner-exit cancellation.
An expired deadline does not return a hardware-owned buffer to callers: verify
quiescence/reset or quarantine it. Cache maintenance, memory barriers, bus address
translation and reachable memory belong to the selected BSP. CPU pointers are
not assumed to be DMA addresses. Buffer capacities dominate many configurations;
report actual buffer, control and alignment costs independently of kernel size.

Phase 3, conditional on hardware: PCIe may be the transport across a SpaceVPX
system. SpaceVPX is a system/backplane context; a PCIe register bridge and a DMA
engine are distinct capabilities. Start with a documented endpoint register/BAR
and mailbox interface if the selected topology supports it, then add bulk DMA.
Root-port support is a separate opt-in scope requiring enumeration, address-window
assignment and bus resource management. No endpoint role, generation, lane count,
switch topology, register aperture or SpaceVPX slot profile is assumed yet.

Before implementing PCIe, record FPGA/IP revision, endpoint versus root-port
role, link generation/width, selected backplane/slot profile, peer software,
BAR layout, address width/windows, interrupt mechanism, reset/link recovery,
throughput, latency and static RAM budget. Define a versioned fixed-width wire
protocol with explicit endian, lengths, ownership, ordering, backpressure and
reset generation. Never transmit native C++ object layouts or CPU pointers.
Bound local and peer-visible DMA windows; validate all peer-supplied ranges with
overflow checks. Define cache/coherency and posted-write ordering from the actual
IP documentation. No IOMMU or hardware isolation is presumed available.

Keep peer buffers and pending requests unavailable after link loss until the
platform proves outstanding accesses have stopped. Reconnect invalidates stale
completion generations. Fault counters and health state feed existing diagnostics.
SpaceVPX deployment does not itself establish radiation tolerance, fault tolerance,
safety certification or standard conformance for this kernel.

Requirements NXR-133 through NXR-138 below are Planned. No firmware implementation,
new build, test, commit or SpaceVPX hardware qualification accompanies this update.

References:

* `AMD AXI DMA simple mode <https://docs.amd.com/r/en-US/pg021_axi_dma/Direct-Register-Mode-Simple-DMA>`_.
* `AMD standalone AXI DMA driver <https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/18842100/>`_.
* `AMD AXI/PCIe bridge guide <https://docs.amd.com/r/en-US/pg194-axi-bridge-pcie-gen3>`_.
* `NASA SpaceVPX interoperability briefing <https://ntrs.nasa.gov/api/citations/20220015433/downloads/SpaceVPX%20Industry%20Outreach%20SOSA%20Presentation_v3.pdf?attachment=true>`_, which identifies PCIe as an expansion-plane option.
