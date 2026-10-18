Five embedded-systems review iterations
=======================================

These are five sequential agent-assisted engineering review passes using the
perspective expected of an experienced embedded-systems programmer. No human
reviewer with claimed years of experience was engaged. The passes are a design
and implementation aid, not independent certification evidence.

Iteration 1: kernel architecture and resource bounds
----------------------------------------------------

Findings: application capacity consumed the idle slot; idle shared priority-zero
time slices; timer catch-up looped once per missed period; static storage lacked
overlap checking; metrics needed a coherent raw-time model.

Changes: separate idle fallback, full configured application capacity, one
catch-up arithmetic operation plus one wake scan, stack-range checks, static
snapshots, and per-thread machine-timer residency accounting. Review also
established the configurable driver and synchronization backlog.

Iteration 2: API contracts and configuration
--------------------------------------------

Findings: stack-to-stack checks alone did not protect thread control blocks;
metrics change public ABI; stack sentinels could be mistaken for overflow
prevention; startup time could be mistaken for assigned runtime.

Changes: reject candidate and registered control-block/stack overlap, propagate
configuration through CMake targets, validate MMIO alignment, document every API
failure case, and state guard and residency limits. Names and storage remain
borrowed for the kernel lifetime.

Iteration 3: timer accounting and Ethernet ownership
----------------------------------------------------

Findings: adapter callbacks needed explicit no-reentrancy and copy/consume
ownership rules; RX callbacks must respect capacity; hardware padding/FCS and
DMA coherency belong to the driver. A compiled archive is not evidence that an
integrated network stack works. A named-symbol audit cannot prove ownership safety.

Changes: document one network owner, borrowed callback frame lifetime, fixed
storage, callback time bounds, hardware duties, and third-party stack audit
limits. Extend host tests for RX busy/error, TX oversize, backpressure, malformed
length, and poll budget. Keep actual MAC and TCP/IP integration explicitly pending.

Iteration 4: assembly ABI and boot-static build
-----------------------------------------------

Findings: frame layout preserves the required integer state by inspection, but
needs target execution tests. Garbage collection could discard constructor
arrays and hide unsupported C++ dynamic initialization from a symbol audit.

Changes: retain constructor/preinitializer arrays explicitly and reject nonempty
sections at link time. Add negative constructor/allocator build checks. Keep
register stress, interrupt masking, snapshot conservation, and stack-depth tests
as documented target release gates.

Iteration 5: final evidence and manual consistency
--------------------------------------------------

Final review found the requirement matrix, configurable behavior, driver status,
and evidence boundaries consistent. It corrected an overstrong reference to
"zero-cost" disabled metrics: disabled counters return zero, while the snapshot
still copies diagnostic fields. No additional serious kernel/ABI issue was
identified by source inspection. Results and remaining target validation limits
are recorded in the validation record alongside this manual.
