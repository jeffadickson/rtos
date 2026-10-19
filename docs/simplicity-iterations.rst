Simplicity refinement: 100 review iterations
================================================================================

This is a second pass over the implemented single-hart services. Each numbered
entry records a specific change or an explicit reviewed decision to retain a
working contract. It is not a claim of 100 new features or independent reviewers.
Builds and local checkpoint commits occur at 25, 50, 75 and 100, with a final push.
Hardware execution and timing qualification remain separate.

Checkpoint 25: application-facing interface
--------------------------------------------------------------------------------

001. Identify public-header internals as avoidable reading complexity.
002. Move wait queues and interrupt serialization declarations into a detail header.
003. Preserve existing namespace/type names to avoid source compatibility churn.
004. Keep the standalone thread header independent of optional services.
005. Keep explicit RTOS_SYNC enablement rather than silently enabling kernel storage.
006. Remove the redundant timed boolean because every blocked wait has a finite deadline.
007. Keep blocked state plus queue membership as the timeout eligibility invariant.
008. Preserve wrap-safe deadline comparison when simplifying timeout checks.
009. Replace event flag magic numbers with named internal constants.
010. Add wait_any to make event intent visible at the call site.
011. Add wait_all to avoid interpreting a positional boolean.
012. Use EventClear::yes/no for the new event APIs.
013. Preserve the previous event wait signature for existing code.
014. Keep zero event masks invalid and failure output unchanged.
015. Add one optional rtos_ipc.hpp convenience include for applications.
016. Retain individual headers for applications preferring explicit dependencies.
017. Include the logger through the convenience header only when enabled.
018. Keep NoWait and tick-based bounded waits; avoid adding a second timeout type.
019. Retain copied queue values rather than adding buffer-loan protocols.
020. Retain nonrecursive mutexes rather than adding recursion counters.
021. Retain priority inheritance because shared devices need bounded inversion behavior.
022. Retain explicit owner-dead recovery because hardware cannot be silently declared safe.
023. Exercise named event consumption and non-consumption through the public convenience header.
024. Compile the public convenience header in the RISCue example.
025. Check host behavior, disabled-feature footprint, enabled firmware and Sphinx before commit.