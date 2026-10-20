Stack paint observations
========================

``RTOS_STACK_WATERMARK=ON`` paints registered application stacks and private idle
at boot, before the architecture frame is built. It defaults off and adds no
thread-control fields. This is separate from the constant-work stack sentinel.

Call ``rtos::stack_usage(thread, result, budget_bytes)`` explicitly. The default
256-byte allowance bounds paint reads; a separate one-word guard check may occur.
The budget is rounded down to whole words and clipped to the registered extent.
An unknown thread returns false and preserves the caller's result. The private
idle identity is available through ``snapshot`` after start.

``watermark_enabled`` distinguishes disabled instrumentation. ``complete`` means
the scan reached the first changed word or the full paintable extent. With an
incomplete scan, ``used_bytes`` is an upper estimate of paint-derived use.
``used_bytes`` includes the initial frame and guard reservation; together with
``untouched_bytes`` it equals capacity when instrumentation is enabled.
``inspected_bytes`` counts paint reads, including a terminating mismatch.

Paint matching does not prove spare capacity: real data can equal the pattern,
and stack space can be reserved without being written. Combine these observations
with compiler stack-usage files, complete call chains and interrupt-stack analysis.
Scanning masks the local scheduling interrupt; choose the allowance explicitly
for the latency budget. No scan runs automatically during dispatch or snapshots.
The ARM stress preset requests full scans only at its diagnostic completion point.
