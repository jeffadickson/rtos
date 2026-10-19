Synchronization, messages and queued printf
================================================================================

Status and configuration
--------------------------------------------------------------------------------

These services are implemented for the current single-hart kernel and exercised
by host state-machine checks and RISCue cross-link/no-heap audits. They have not
been qualified on an FPGA or against worst-case interrupt latency. External
peripheral interrupt dispatch is still absent: ISR entry points are tested with
the host context model and require a board port that invokes them from a supported
handler. They do not enable UART interrupts by themselves.

Set RTOS_SYNC=ON for queues, mutexes, semaphores, event flags and AtomicU32.
Set RTOS_LOGGING=ON as well for the formatter/logger. Logging requires sync.
Both default off; nano excludes both. Unused functions are removed by firmware
section garbage collection, while all synchronization-enabled threads carry the
shared wait/ownership metadata. Separate per-primitive switches remain future work.
RTOS_IPC_MAX_MESSAGE_BYTES defaults to 128 (range 1..512); it bounds copied message
work while interrupts are masked. RTOS_MAX_HELD_MUTEXES defaults to 8 (range 1..255).
Choose smaller limits when interrupt latency and stack budgets require them.

All threads, synchronization objects, queues and logger instances must have
permanent storage established before scheduling. There is no dynamic deletion,
wait-set API, heap-backed message or cancellation API. Do not modify Thread's
kernel-owned links, priority fields or state. The types are deliberately
noncopyable. The kernel does not enforce a registry seal for IPC objects; storage
lifetime is an application contract. Calls from main before start fail with
Result::context, except nonblocking snapshots and AtomicU32 operations.

Every timed operation accepts a relative Tick duration of 1..MaxWait (INT32_MAX).
NoWait is zero; infinity is deliberately not supplied. Invalid durations fail
before changing resources, even when an operation could otherwise complete.
Tick timeout means the thread becomes ready at its deadline; execution still
depends on priority scheduling. Conditions and waiter enrollment are protected
by one interrupt-masked section. A transfer and its timeout cannot both succeed.

Messages
--------------------------------------------------------------------------------

.. code-block:: cpp

   #include "rtos_queue.hpp"
   struct Sample { uint32_t channel; int32_t value; };
   static rtos::Queue<Sample, 8> samples;

   // Producer thread; a bounded copy transfers value ownership.
   auto status = samples.send({2, 1234}, rtos::NoWait);
   // Consumer thread; failed receive leaves sample unchanged.
   Sample sample{};
   status = samples.receive(sample, 100); // ticks, not milliseconds

Messages must be trivially copyable and trivially default-constructible value
types, with sizeof(T) within the configured cap. Pointer payloads do not transfer
ownership of the pointed-to memory; prefer values. FIFO message order is distinct
from priority-ordered waiters, whose ties follow enrollment order. A waiting
sender retains a private message copy on its boot-provided stack; callers must
account for that stack cost. A selected receiver is filled before it becomes
ready, and a freed slot is reserved for a selected waiting sender before other
callers can steal it. NoWait returns full/empty explicitly. Full/empty counters
count observed inability to transfer, including a timed call that later succeeds;
they are not packet-loss counters. Completed timeout counts update when the
waiting call resumes.

try_send_from_isr and try_receive_from_isr never sleep. They require a supported
interrupt context and copy under the same interrupt-mask protocol. This is
single-hart serialization, not a lock-free or SMP queue.

Events and semaphores
--------------------------------------------------------------------------------

Semaphore(initial, maximum) rejects invalid limits through operation results.
acquire(timeout) consumes a token or waits; release grants the selected waiter
before exposing a count to contenders. A release at maximum returns overflow.
release_from_isr is the nonblocking interrupt variant. Semaphores have no owner
or priority inheritance and must not replace mutexes for shared data protection.

Prefer EventFlags::wait_any or wait_all, with EventClear::yes when bits should
be consumed. The original wait(mask, observed, timeout, all, clear) remains
available for compatibility. Both forms support
atomic clear-on-success; failure preserves observed. All 32 bits are usable;
a zero mask is invalid. Repeated set operations coalesce. Non-clearing waiters
observe the bit snapshot at set; consuming waiters receive bits by priority,
with remaining bits reevaluated after each grant. set_from_isr is nonblocking.
A flag does not transfer buffer ownership; protect associated shared data.

Mutexes and devices
--------------------------------------------------------------------------------

.. code-block:: cpp

   #include "rtos_mutex.hpp"
   static rtos::DeviceTransactions device;
   auto status = device.transact(100, [&]() {
       // Write command, wait for completion, read result as one owned transaction.
       // Actual register map, barriers and completion source belong to the BSP.
       return rtos::Result::ok;
   });

Mutex::lock(timeout), unlock and LockGuard protect complete logical transactions.
They are nonrecursive; self-lock and detected mutex dependency cycles return
deadlock. Only the owner may unlock. Waiters donate effective priority through
mutex ownership chains. Unlock, timeout and set_priority recompute donations
across all remaining owned mutexes. ThreadInfo reports base priority and
effective_priority (equal to base when sync is disabled).

At most RTOS_MAX_HELD_MUTEXES locks may be held by a thread. Waiter and donation
scans are bounded by registered thread and held-lock limits; these are not
constant-time operations. Mutex::priority_changed and owner_exit are internal
kernel hooks, not application lifecycle APIs.

Returning from a thread or calling exit while holding a mutex marks it owner-dead.
Waiters receive owner_dead without ownership; protected data/hardware is not
silently declared safe. After establishing quiescence externally, a thread can
call recover_after_quiescence before locking again. The kernel supplies no DMA
abort, AXI reset or general thread-kill mechanism. Cycles involving semaphores,
queues or application predicates still require application design discipline.

Atomic words
--------------------------------------------------------------------------------

AtomicU32 supplies load, store, exchange, compare_exchange, fetch_add, fetch_or
and fetch_and. compare_exchange updates expected on mismatch; fetch_add wraps
unsigned arithmetic. These operate on ordinary RAM under short interrupt masking,
without requiring the RISC-V A extension or libatomic. They are not C++ standard
atomics, lock-free operations, MMIO helpers, DMA coherence or multicore locks.
All access must use the object; no raw aliasing. Port compiler barriers protect
publication across supported kernel/interrupt contexts on this hart.

Queued printf
--------------------------------------------------------------------------------

.. code-block:: cpp

   #include "rtos_log.hpp"
   static rtos::Logger<8, 64> console;
   // Any application thread:
   auto result = console.printf("channel=%u value=%d\n", 2u, 1234);
   // A permanent output thread periodically calls:
   console.drain(uart_write_some, uart_context, 32);

This is console.printf, not an override of libc printf. Formatting uses a private
bounded stack record with interrupts enabled; publication copies the finished
record under a bounded critical section. No global formatting lock or writable
reservation survives a preempted producer. UART work occurs only in drain.
A full queue drops the new record and returns full; producers never wait for UART.
Strings and format pointers must be valid readable memory during formatting;
there is no protected copy-from-user or general fault recovery.

Supported conversions are %d, %i, %u, %x, %X (including l and ll integer lengths),
%s, %c, %p and %%. Supply exactly matching variadic argument types; %p requires
void*. Width, precision, floating point, %n and other specifiers return invalid
and enqueue nothing. Format scanning is limited to 256 characters and %s to
96 characters. Truncation is reported; output is length-delimited and need not
contain a NUL. Bytes and UTF-8 sequences may be truncated. Newlines are explicit.
The formatter is bounded but its time and combined trap/call stack require target
measurement; do not assume a 512-byte thread stack is adequate for logging.

write(text, length) sends preformatted data. write_from_isr is the only logger
ISR API; it copies at most MessageBytes and never formats variadic arguments.
No shared thread-local formatter buffer is used, so nested contexts have their
own bounded stack records. The UART callback consumes or copies accepted bytes
before return, returns 0 for backpressure, and a negative value only when that
call consumed no bytes. Retaining the supplied pointer or starting DMA violates
the contract. Return values larger than offered are errors. A pending record
and its offset survive partial writes and backpressure, preserving record order.

vprintf(format, va_list) supports application variadic wrappers and preserves
the caller's va_list using va_copy. GCC/Clang check argument types for printf
and vprintf calls; runtime still rejects unsupported format features.

A drain returns empty when no record was available, ok when it made progress
and exhausted available output, busy for pending output/backpressure, or an
explicit error. Completing a record resets its reported offset to zero.

A drain call has a byte budget and at most 16 loop iterations. Its callback must
also perform bounded work; no software budget can bound a hung MMIO operation.
NoWait drain ownership prevents simultaneous drains of one logger. All console
writers must use the same logger to avoid interleaving. Negative writer errors
retain pending bytes for explicit retry; resetting a device mid-record may leave
a partial external line. A drain thread exiting while holding its mutex faults
the logger; automatic recovery/replay is not supplied. Use a permanent drain thread.
Logger snapshots report accepted records, full drops, truncation, errors, sent
records, pending state and queue occupancy. Sent means synchronously accepted by
the writer, not necessarily physically shifted onto the wire.

Memory and verification
--------------------------------------------------------------------------------

Logger<8,64> has 528 bytes of queued record payload (8 times 66), plus one pending
record, queue/mutex controls, counters and alignment. Exact target object size is
720 bytes in the checkpoint-100 RV32 firmware symbol listing (console size
0x2d0), including the pending record and queue/mutex controls. Producer formatting uses
additional existing stack space, never a heap. The full example includes two
1024-byte application stacks and a 1024-byte idle stack; its RAM is not the size
of the synchronization primitives alone. Current nano measurements, including
the separately reserved boot stack, are in :doc:`footprint`.

Host fixtures model scheduling boundaries, direct handoff, priority donation,
wraparound timeout, mixed event consumption, queue pressure and short writes.
They are not physical multicore stress tests or FPGA execution. Hardware IRQ,
UART, latency, stack high-water and fault/reset qualification remain outstanding.
See sync-iterations for the 100-step implementation record and future acceptance
requirements in the canonical tracker.

Original implementation checkpoint-100 firmware sections
--------------------------------------------------------------------------------

.. list-table:: RV32 MinSizeRel examples, bytes
   :header-rows: 1

   * - Profile
     - Code/read-only
     - Initialized data
     - Zero-fill
   * - nano
     - 3648
     - 0
     - 1152
   * - sync example
     - 7532
     - 144
     - 3504
   * - logging example
     - 11072
     - 860
     - 3504

The shared 4096-byte boot-stack reservation and section alignment are additional.
These are complete example images with different application stacks and capacities,
not isolated incremental primitive sizes. The logger's initialized state is
included in the logging data column. The logging sink counts accepted bytes and
does not operate a hardware UART.

The table above records the original implementation pass. Later simplicity-pass
measurements and compiler frame-size comparisons are recorded in
simplicity-iterations. Compiler frame reports exclude callee and interrupt
stack use; they do not establish a safe thread stack size.
