Formal Verification & Compile-Time Thread Safety
================================================

Preventing thread defects by construction combines two layers:

1. **Zero-overhead compile-time concurrency safety** (``include/rtos_safe.hpp``)
   that makes entire defect classes unrepresentable in C++20.
2. **Machine-checked SMT proofs in Dafny** (``formal/*.dfy``) verified by Z3
   directly inside CTest.

Compile-Time Prevention (``rtos_safe.hpp``)
-------------------------------------------

* **Deadlock impossibility** (``OrderedLock<T, Level>`` + ``LockToken<Level>``):
  acquiring a lock requires a compile-time ``LockToken<PrevLevel>`` satisfying
  ``static_assert(Level > PrevLevel)``. Cyclic lock acquisition cannot compile,
  and protected state ``T`` is private inside the lock wrapper (eliminating
  unsynchronized data races).
* **Zero priority inversion & single blocking**
  (``CeilingResource<T, CeilingPriority>``): implements the Immediate Priority
  Ceiling Protocol (IPCP/SRP), raising ``effective_priority`` to
  ``CeilingPriority`` upon entry and rejecting callers whose base priority
  exceeds the ceiling.
* **Stack/TCB binding** (``StaticTask<StackBytes, Priority>``): bundles a
  non-copyable ``Thread`` and aligned ``Stack`` into one static object.
* **ISR vs. thread capability separation** (``TypedChannel<T, Capacity>``):
  exposes distinct ``ThreadSender``, ``ThreadReceiver``, and ``IsrSender``
  endpoints so calling blocking sends from an ISR handle is a compile error.

Dafny Formal Proofs (``formal/*.dfy``)
--------------------------------------

When ``dafny`` and ``z3`` are present, CTest automatically executes all four
formal proof suites:

* ``formal/SchedulerProof.dfy`` (9 verified): proves ``order[]`` remains a
  bijection across ``MoveToBack``, proves ``Pick`` always selects a ``Ready``
  thread with maximal effective priority and FIFO tie-breaking, proves strict
  rank progress (starvation freedom), and proves 32-bit wrap-safe deadline
  comparisons exact for all ``1 <= delay < 2^31``.
* ``formal/LockFreedomProof.dfy`` (7 verified): proves strict lock hierarchies
  admit zero wait cycles of any length (deadlock impossibility) and proves IPCP
  prevents preemption by any co-user of a ceiling-protected resource.
* ``formal/DmaOwnershipProof.dfy`` (10 verified): proves DMA staging is never
  ``Idle`` while hardware owns the buffer, failed ``stop()`` always quarantines,
  non-wrapping generation tickets reject all stale completions, and
  overflow-free ``ranges_overlap`` is symmetric and exact.
* ``formal/TmrVoterProof.dfy`` (4 verified): proves over ``bv32`` bitvectors
  that 2-of-3 majority voting masks any arbitrary single-replica corruption,
  mismatch detection is exact, and 1-cycle scrubbing restores exact triple
  equality.
