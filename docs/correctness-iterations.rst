Correctness refinement: 100 review iterations
================================================================================

This pass follows the implementation and simplicity passes. A numbered iteration
is one focused code/contract review, with a change or a retained decision; these
are not 100 independent reviewers or 100 new features. Builds and checkpoint
commits occur only at 25, 50, 75 and 100, followed by one push. Host models and
cross-compilation do not establish hardware execution or timing qualification.

Checkpoint 25: boot storage and scheduler boundaries
--------------------------------------------------------------------------------

001. Trace every registration rejection before the first registry mutation.
002. Retain boot-only registration and permanent caller-owned storage.
003. Retain duplicate detection in the bounded pointer registry.
004. Retain capacity rejection before insertion.
005. Confirm initialization follows successful registration without allocation.
006. Review stack-versus-stack overlap checks.
007. Review stack-versus-control overlap checks.
008. Identify missing candidate-control versus registered-control comparison.
009. Add one internal conflicts helper covering both registered storage classes.
010. Use that helper for both candidate stack and control block.
011. Review private idle-stack overlap at start.
012. Extend start checks to registered control blocks overlapping the idle stack.
013. Extend start checks to registered storage overlapping the idle control block.
014. Replace endpoint addition in overlap comparisons with address differences.
015. Treat empty ranges as non-overlapping.
016. Preserve half-open ranges so adjacent allocations remain valid.
017. Exercise overlap symmetry across a bounded address/length matrix.
018. Exercise containment in both argument orders.
019. Exercise low-address and near-UINTPTR_MAX ranges without dereferencing them.
020. Exercise the combined conflict helper against stack and control storage.
021. Retain caller responsibility for actual writable object extent and lifetime.
022. Recheck strict-priority selection and equal-priority rotation fixtures.
023. Recheck modular sleep wakeup and constant-time missed-tick calculation fixtures.
024. Update the registration contract with both control-block comparisons.
025. Gate the checkpoint on host models, firmware allocation audit and Sphinx.

Checkpoint 50: bounded formatting and console ownership
--------------------------------------------------------------------------------

026. Review empty formatting as a successful zero-byte record.
027. Exercise null destination rejection in the internal formatter.
028. Exercise zero destination capacity without writes.
029. Exercise null format rejection.
030. Place canaries around an exact-capacity destination.
031. Check exact capacity does not report truncation.
032. Reduce capacity by one and check truncation without overwriting the next byte.
033. Exercise embedded NUL character output using the returned byte count.
034. Exercise uppercase hexadecimal conversion.
035. Exercise unsigned-long maximum conversion on the host ABI.
036. Exercise signed-long minimum magnitude conversion on the host ABI.
037. Exercise null pointer rendering without dereferencing the pointer.
038. Verify unsupported percent-n does not modify its argument.
039. Verify unsupported width, floating point and length-modified float rejection.
040. Verify dangling percent and excessive length modifiers are invalid.
041. Verify scanning still rejects an unsupported conversion after output fills.
042. Exercise a terminated 95-byte string without truncation.
043. Exercise the conservative 96-byte string scanning limit.
044. Exercise null string rendering as six explicit bytes.
045. Exercise terminators immediately inside and outside the 256-byte format bound.
046. Fill a logger with empty records and check the 16-iteration drain limit.
047. Verify empty records never invoke the transport writer.
048. Enqueue from a second modeled producer during an unlocked transport callback.
049. Check coherent pending metrics, busy drain ownership and whole-record ordering.
050. Validate zero-budget rejection and run both host suites and strict Sphinx.

Checkpoint 75: timeout ordering and wait cleanup
--------------------------------------------------------------------------------

051. Review serialization of a tick and signal at the same logical deadline.
052. Exercise tick-first ordering: the waiter times out before the release.
053. Check that the later release retains a semaphore token for another acquire.
054. Exercise signal-first ordering: the waiter succeeds before the tick.
055. Check that a later tick cannot time out an already completed wait.
056. Check completed wait queue and payload pointers are cleared.
057. Exercise a one-tick wait crossing UINT32_MAX to zero.
058. Exercise the maximum permitted relative event wait.
059. Preserve event output on timeout at the maximum deadline.
060. Check the timed-out event waiter is removed from the queue.
061. Fill a one-slot queue and enroll two blocked producers.
062. Expire the first producer while the second remains blocked.
063. Check expired producer staging pointers are cleared before another receive.
064. Receive the buffered value and grant the remaining producer's value.
065. Verify the timed-out value is never delivered later.
066. Verify send-waiter count and completed-timeout count after both returns.
067. Preserve receive output on the resulting empty queue.
068. Combine wait-all observation with a higher-priority consuming wait-any.
069. Preserve the broadcast snapshot for non-consuming waiters.
070. Verify only the consumer's observed bit is removed from stored event flags.
071. Exit a mutex owner with two enrolled waiters and inherited priority.
072. Verify both waiters receive owner_dead and all ownership links are cleared.
073. Verify explicit recovery permits a later lock after modeled quiescence.
074. Check diagnostic counter saturation and retain bounded single-hart semantics.
075. Refresh requirement evidence and gate both host profiles and Sphinx.
