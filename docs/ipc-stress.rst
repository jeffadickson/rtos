Reproducible IPC checks
================================================================================

The host suite adds four fixed xorshift32 seeds: 1, 42, 3735928559 and 4294967295.
Each runs 50,000 operations at queue capacities one, three and eight: 600,000
operations total. A failure reports seed, step, operation and violated contract.
Run one directly as ``build/host/ipc_random_test 42`` to reproduce it.

The copied-FIFO oracle shifts a fixed array instead of using ring arithmetic.
Separate scalar oracles track semaphore tokens, sticky event bits and atomic
word state. Checks include FIFO preservation, empty-output preservation, full
and overflow results, event any/all consumption, compare/exchange success and
mismatch, fetch-operation prior values, invalid waits, ISR entry points and
critical-section balance. All reference storage has fixed capacity.

``ipc_wait_order`` enrolls three actual wait nodes through the host parking model.
It covers all six enrollment orders, 27 priority assignments, two service types
(semaphore and queued receives), and four timeout/priority-change combinations:
1,296 campaigns. An independent stable ordering of IDs predicts the grant order.
Deadlines cross UINT32_MAX; expired nodes must detach, preserve receive output,
and remain detached when a late token or message arrives.

These model checks exercise C++ contracts and deterministic state transitions.
They do not execute interrupt assembly or simulate simultaneous cores. The ARM
suite separately executes the real kernel and exception paths. Neither check
establishes SMP safety, worst-case timing, DMA coherence or hardware qualification.

The ordinary host build includes both fixtures. ``ctest --preset host`` runs
the fixed seeds and waiter permutations. Assertions remain enabled in host
fixtures even when using an optimized build.

Thread lifecycle stress
--------------------------------------------------------------------------------

``thread_stress_test`` runs the same four fixed seeds, each with 128 campaigns
of 512 state transitions (262,144 transitions across the suite). Up to eight
registered threads alternate ready/sleeping states, change priorities, cross
tick wrap and retire permanently within each campaign. An independent 64-bit
clock predicts wakeups without using the scheduler's modular deadline comparison.
Each selection must choose a registered ready thread at the highest priority;
nonrotating selection must retain an eligible current thread. Stable equal-priority
phases require every thread to run exactly once per round, and all-finished
phases must remain idle after time advances. Storage is fixed and failures report
seed, PRNG state, campaign and step. Reproduce with
``build/host/thread_stress_test 42``. These are host scheduler checks, not real
context switches or concurrent CPU execution. The memory-check presets include
every fixed seed.
