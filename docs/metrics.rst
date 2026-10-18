System metrics and CPU usage timer
==================================

``snapshot`` provides a stable row for each application thread and idle. Rows
contain identity, name, priority, state, whether currently running, stack size,
guard status, cumulative runtime cycles, and dispatches. ``SystemInfo`` contains
logical ticks, actual supplied clock frequency, integer timer period, elapsed
cycles since scheduler startup, counts, and the metrics-enabled flag.

Use a static diagnostic buffer large enough for applications plus idle::

   static rtos::ThreadInfo rows[RTOS_MAX_THREADS + 1];
   auto info = rtos::snapshot(rows, RTOS_MAX_THREADS + 1);
   for (size_t i = 0; i < info.written_threads; ++i) {
       // Export rows[i].name, priority, state and runtime_cycles via your console.
   }

Call from a diagnostic thread, not an interrupt. A snapshot masks interrupts
while accounting and copying, so use a modest sampling rate such as once per
second and measure the cost for the configured capacity. The returned records
are value copies except identity/name pointers, whose storage must stay valid.

Meaning of CPU time
-------------------

With metrics enabled, elapsed machine-timer cycles are charged to the currently
dispatched thread. Before choosing the next thread, the trap handler charges
the interval since its last accounting point. A snapshot also updates the
current thread's accumulated interval under the lock. Repeated snapshots do not
double-count time. Dispatch count increments on each scheduler dispatch,
including redispatch of the same thread.

This is **scheduled residency**, not pure application instruction execution.
It includes timer/ECALL trap work before the accounting point in the outgoing
thread and subsequent selection/restore overhead in the incoming thread.
Time between kernel timer initialization and first dispatch is unassigned,
so summed thread totals may be slightly below total elapsed time. Idle residency
measures lack of ready work. It does not measure energy saved or instruction
utilization. Do not reset ``mtime`` after startup.

Interval utilization
--------------------

Take snapshots A and B and match rows by ``id``. For each thread compute:

.. math::

   U_i = 100 \frac{C_{i,B} - C_{i,A}}{E_B - E_A}

where C is runtime_cycles and E is elapsed_cycles. A nonzero denominator is
required. Busy percentage is approximately 100 minus idle percentage. Convert
cycles to seconds by dividing by ``clock_hz``. Perform floating-point formatting
on the host or use bounded integer scaling; this RISCue port has no FPU.
If multiplying a 64-bit delta by 100 or 10000, bound the sampling interval to
avoid integer overflow. The kernel intentionally returns raw counters.

With ``RTOS_METRICS=OFF``, runtime, dispatch, and elapsed values are zero and
``metrics_enabled`` is false. Thread/priority/state information remains available.
Guard status is always true if guard checking is disabled; it is not a measured
stack high-water mark in either configuration.
