Implementation refinement: 300 passes
=====================================

This campaign uses focused implementation, contract and verification passes,
not 300 new services or 300 independent reviewers. Status is emitted for every
numbered pass. Builds and commits occur at 50-pass checkpoints, followed by
independent kernel, embedded-port and verification/documentation reviews. Review
findings are addressed in following passes and rechecked before the final push.
Existing uncommitted project work is preserved in the first checkpoint.

Passes 001-050: bounded stack observations
--------------------------------------------------------------------------------

001. Preserve the existing requested implementation as the checkpoint baseline.
002. Retain boot-only registration and permanent finished-thread storage.
003. Retain finite IPC deadlines and copied-message lifetime contracts.
004. Review the ARM BASEPRI/SVC atomic enrollment-and-park contract.
005. Choose explicit budgeted watermark scans outside scheduling/snapshots.
006. Paint stacks before the architecture constructs their initial frame.
007. Add allocation-free bounded prefix scanning.
008. Default watermark instrumentation off and validate its compile-time switch.
009. Add a result type without enlarging thread control blocks.
010. Add an explicit diagnostic API with a 256-byte default allowance.
011. Account separately for the optional one-word guard read.
012. Preserve output when the thread identity is unregistered.
013. Expose a metadata-only disabled result.
014. Include initial frame and guard reservation in paint-derived use.
015. Document pattern collisions and unwritten reserved frames.
016. Cover zero-length paint.
017. Cover empty scans without dereferencing a pointer.
018. Cover complete-extent painting.
019. Cover zero-budget read accounting.
020. Cover a single-word allowance.
021. Cover incomplete prefix estimates.
022. Cover the exact full-budget boundary.
023. Cover oversized allowances without expanding the memory extent.
024. Cover an immediate mismatch.
025. Cover a mismatch within a one-word allowance.
026. Cover a mismatch just outside the budget.
027. Cover a mismatch at the budget boundary.
028. Verify the scanner leaves source memory unchanged.
029. Cover a change at the highest stack word.
030. Demonstrate the observable limitation of paint collisions.
031. Cover a guarded subextent.
032. Cover a mismatch immediately above the guard.
033. Cover repeatable painting without hidden state.
034. Exhaust small mismatch positions and read allowances.
035. Cover an empty extent with maximum allowance.
036. Enable observations in the ARM stress preset.
037. Exercise unregistered identity rejection on ARM.
038. Exercise a zero-byte allowance on a real thread stack.
039. Exercise a sub-word allowance on ARM.
040. Observe all real worker and idle stacks.
041. Check reported capacity conservation.
042. Emit stack use/capacity in simulator logs.
043. Add implementation requirement NXR-151.
044. Separate stack guards from watermark documentation.
045. Document scan latency and incomplete observations.
046. Record numbered passes and the checkpoint/review policy here.
047. Regenerate StrictDoc-format and requirements traceability views.
048. Explicitly keep the nano preset's instrumentation disabled.
049. Inspect source diffs and static-allocation boundaries before the checkpoint.
050. Build, validate and commit the first checkpoint, then request panel review.
