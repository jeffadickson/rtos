Scheduling and RISCue port
==========================

Priority and fairness
---------------------

Application priorities are integers 0 through 255. A larger number takes
precedence. At every timer trap, the scheduler wakes eligible sleepers and
selects the highest-priority ready thread. With round-robin enabled, equal
priorities rotate at every tick. Explicit ``yield()`` rotates equal priorities
even if timer time slicing is disabled. A higher-priority ready thread always
wins, so yield does not let lower priorities run. Sleep, finish, or reduce the
priority to allow lower-priority execution.

The private idle thread is outside the application registry and capacity. It
runs only when no application is ready; priority-zero application work takes
precedence over idle. Idle executes ``wfi`` repeatedly; RISCue currently treats ``wfi`` as a
NOP, so idle time is accounting information, not evidence of power savings.

Sleeping deadlines use unsigned modular tick arithmetic. A sleep delay is at
most INT32_MAX ticks. Zero delay means yield. A one-tick sleep may last less
than one complete interval because it ends at the next tick boundary.

Timer and bounded work
----------------------

``mtime`` increments once per RISCue core clock. The timer period is the integer
quotient of supplied clock frequency and requested tick frequency. Prefer exact
divisors. The compare register is updated high-word-max, low-word, high-word,
preventing a transient early deadline during an RV32 write.

Delayed interrupts calculate the number of elapsed periods once using unsigned
64-bit arithmetic and scan sleeping threads once. Missed periods do not cause
a software loop per tick. Division may use the compiler's integer helper; this
is bounded arithmetic, not a measured latency guarantee. Tick-count wraparound
is supported. The system halts if a single catch-up exceeds INT32_MAX ticks.
The 64-bit hardware timer must not wrap or be reset during an operating session.

Thread selection and wake scanning are O(configured thread count). Snapshot
copying masks interrupts and is O(number of rows copied), bounded by the static
registry. Keep diagnostics infrequent enough for the application's latency budget.

Context and inline assembly
---------------------------

CSR access, interrupt masking, idle, and ECALL use C++ inline assembly with
register constraints and compiler memory clobbers where ordering matters.
Compiler register allocation remains available around these operations.

``ports/riscue/context.S`` is the required trap entry boundary. Asynchronous
preemption can interrupt any instruction, so it saves all integer registers,
including caller-saved temporaries, plus ``mepc`` and ``mstatus``. A normal C++
function prologue cannot perform this task before it has already modified
registers. The 144-byte frame maintains 16-byte stack alignment. The saved
stack pointer restores the pre-trap value. ECALL advances ``mepc`` by four;
this port emits the standard 32-bit ECALL instruction.

The port owns ``mtvec``, ``mie``, machine-mode ECALL cause 11, and machine timer
interrupt cause 7. Other traps halt with interrupts disabled. Nested interrupts,
external interrupt dispatch, floating-point/vector state, TLS, SMP, and user
mode are not implemented. Do not enable an external interrupt before adding
and verifying a port-level dispatcher and acknowledgement path.

Critical sections
-----------------

Kernel operations use a private lock that clears MIE and restores its prior
state. A sleeping thread may have a saved context with interrupts disabled;
after it resumes, its lock destructor restores the original MIE value.
These locks serialize one hart only. No ``A`` extension atomics are assumed.
Do not yield, sleep, exit, or change priorities while using an application critical
section to protect shared data: rescheduling permits another thread to run.

Application shared data requires an application synchronization design.
``volatile`` neither protects compound operations nor establishes ownership.
Enable RTOS_SYNC for priority-inheriting mutexes, semaphores, copied queues and
events; see :doc:`synchronization`. Their ISR methods require a verified interrupt
context; the port still lacks peripheral dispatch. See :doc:`assembly-port` for
annotated assembly and :doc:`source-reference` for the commented C++ headers.
