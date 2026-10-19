Synchronization implementation iterations
==========================================

This log records 100 concrete implementation/review steps in four batches.
Builds and local commits occur only at completed 25-step checkpoints; push follows
checkpoint 100. Host state-machine evidence is distinct from board execution.

Checkpoint 25: wait core, semaphore and events
----------------------------------------------

001. Define explicit synchronization result codes without exceptions.
002. Define finite tick timeout limits and immediate NoWait behavior.
003. Store a single private wait node per thread when synchronization is enabled.
004. Add blocked scheduling state without changing the assembly context offset.
005. Select runnable threads using effective priority when synchronization is enabled.
006. Mask interrupts with a scoped, noncopyable critical-section guard.
007. Supply a kernel backend for current-thread and interrupt-context validation.
008. Enroll waiters and change blocked state under the same critical section.
009. Preserve FIFO enrollment order for equal-priority waiters.
010. Select the highest effective-priority eligible waiter with a bounded scan.
011. Unlink completed waiters and clear borrowed pointers before making them ready.
012. Expire waits through the scheduler's wrap-safe tick processing.
013. Distinguish thread and ISR APIs and reject calls from an invalid context.
014. Defer rescheduling until the operation's outer critical section exits.
015. Validate semaphore initial and maximum counts without runtime allocation.
016. Return explicit empty and overflow results without changing semaphore count.
017. Hand a released semaphore token directly to a selected waiter.
018. Expose coherent semaphore count and waiter snapshots.
019. Implement event wait-any and wait-all with returned matching bits.
020. Implement atomic clear-on-success and preserve output on failed waits.
021. Resolve consuming event waiters by priority and reevaluate available bits.
022. Support non-clearing broadcast by waking each eligible waiter.
023. Expose bounded event update and snapshot operations.
024. Add deterministic host scenarios for wake-before-return and wraparound timeout.
025. Compile synchronization out of nano and cross-link an enabled firmware example.

Checkpoint 50: typed queues and atomic words
--------------------------------------------------------------------------------

026. Add a fixed-capacity typed Queue with no runtime allocation.
027. Reject nontrivial message types and zero capacities at compile time.
028. Bound individual message copies with RTOS_IPC_MAX_MESSAGE_BYTES.
029. Keep FIFO message order in fixed ring storage.
030. Copy successful sends before returning ownership to the caller.
031. Deliver directly into a blocked receiver's output before waking it.
032. Preserve receive output on empty, invalid and timeout returns.
033. Return full without silently overwriting unread messages.
034. Sleep full-queue senders with a private staged message copy.
035. Fill a freed slot from the selected waiting sender before new callers can steal it.
036. Order queue waiters by effective priority and enrollment order.
037. Keep timeout and transfer outcomes mutually exclusive through common wait removal.
038. Provide bounded nonblocking ISR send with explicit context checks.
039. Provide bounded nonblocking ISR receive with explicit context checks.
040. Expose queue capacity, depth and high-water snapshots.
041. Count full/empty observations and completed timeouts with saturation.
042. Make queue objects noncopyable to preserve waiter link ownership.
043. Add interrupt-serialized 32-bit RAM load and store operations.
044. Add atomic exchange returning the previous value.
045. Add compare/exchange that updates expected on mismatch.
046. Add unsigned wrapping fetch-add with no libatomic dependency.
047. Add fetch-or and fetch-and for shared flag words.
048. Explicitly restrict atomic words to single-hart RAM access through the API.
049. Exercise direct handoff, priority selection, timeout and ISR-context queue paths.
050. Cross-link queue and atomic services in the enabled RISCue example.
