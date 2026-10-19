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
