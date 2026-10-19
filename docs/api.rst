Thread API and error contracts
==============================

Public header: ``include/rtos.hpp``. All calls are single-hart thread/main
context operations. None is approved for use from an ISR. Thread fields are
kernel-owned after registration: application code must not mutate them directly.

Public types
------------

.. cpp:struct:: rtos::Thread

   Boot-registered control block with caller-owned permanent storage.

.. cpp:struct:: rtos::ThreadInfo

   Snapshot row described in the metrics chapter and public header.

.. cpp:struct:: rtos::SystemInfo

   Snapshot timing/configuration/count information.

.. cpp:type:: rtos::Entry

   Alias for ``void (*)(void*)``.

.. cpp:type:: rtos::Tick

   Alias for ``uint32_t``.

Objects and registration
------------------------

``Thread`` is a static control block. ``Stack<Bytes>`` provides 16-byte-aligned
storage with compile-time size checks. Construct objects with their default
member initializers; do not fabricate an unused state in an existing live object.

.. cpp:function:: bool rtos::create(Thread& thread, Entry entry, void* argument, uint8_t priority, void* stack, size_t bytes, const char* name = nullptr)

   Register at boot, from main, before start. Returns false for a sealed registry,
   non-unused control block, null entry or stack, insufficient capacity, a size
   below 512, non-16-byte stack alignment/size, address overflow, overlap with
   another registered stack or control block, or a candidate control block
   overlapping any registered stack or control block. Failure leaves the
   registry and control block unchanged.
   Pointer validity and actual writable storage size are the caller's responsibility.
   Other application data must not alias a stack. The typed Stack overload
   infers storage size and alignment. Name is an optional borrowed immutable string.

.. cpp:function:: void rtos::start(uint32_t clock_hz, uint32_t tick_hz = RTOS_TICK_HZ)

   Never returns. Seals registration, initializes idle and the timer, then dispatches.
   Invalid zero tick rate, clock lower than tick rate, repeated start, or idle-stack
   or idle-control storage overlap halts. The clock argument must match hardware. Zero application threads
   is valid and runs idle only. Runtime constructors are not invoked by startup.

Thread execution
----------------

.. cpp:function:: void rtos::yield()

   Reschedule. Before start this is a no-op. Equal-priority peers rotate, but
   lower-priority ready threads cannot displace a higher-priority caller.

.. cpp:function:: bool rtos::sleep_for(Tick ticks)

   Return false before start or for a delay greater than INT32_MAX. Otherwise
   suspend until the modular deadline; zero yields. Returns true after resuming.

.. cpp:function:: Tick rtos::ticks()

   Return the logical tick counter under the kernel interrupt lock. Wraps at
   2^32. Before start the value is zero.

.. cpp:function:: void rtos::exit()

   Never returns. Marks the current thread finished and reschedules. Entry-function
   return calls this automatically. Calling before dispatch or from idle halts.
   Finished storage remains reserved; no implicit deletion or destructor runs.
   Release application resources before returning. In particular, exit is not
   stack unwinding and does not invoke destructors of active local objects.

.. cpp:function:: bool rtos::set_priority(Thread& thread, uint8_t priority)

   Return false for an unregistered or finished thread. Otherwise update the
   priority and reschedule if running. Valid at boot too. A running caller may
   be preempted before this call returns. Priority values are already bounded
   by the parameter's 8-bit type.

Diagnostics
-----------

.. cpp:function:: SystemInfo rtos::snapshot(ThreadInfo* output, size_t capacity)

   Copy a coherent diagnostic view into caller-owned storage. Null output or
   zero capacity is valid. ``required_threads`` is total available rows;
   ``written_threads`` is rows actually copied. Truncation is explicit and is
   not an error. Registry order is stable; idle is the final row after start.
   Before start, only registered applications are listed. The running thread's
   state is ready with ``current`` true. See the metrics chapter for counter
   interpretation and interval CPU usage calculation.

Full declarations
-----------------

.. literalinclude:: ../include/rtos.hpp
   :language: cpp
