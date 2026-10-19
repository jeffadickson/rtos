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
Checkpoint 50: build structure and supported profiles
--------------------------------------------------------------------------------

026. Move firmware target setup into one focused CMake include.
027. Keep compiler, board and allocation checks in the same firmware path.
028. Move host-only targets into a separate test include.
029. Declare each host test with one common helper.
030. Preserve existing CTest names and commands for compatibility.
031. Centralize assertion enablement for debug and release configurations.
032. Apply one consistent compiler warning policy to host fixtures.
033. Preserve the memory fixture's no-builtin compiler requirement.
034. Preserve allocator audit dependencies on firmware executable targets.
035. Preserve linker script dependency tracking and map generation.
036. Preserve the bounded web link probe separately from the kernel example.
037. Compile shared synchronization model sources once for host fixtures.
038. Keep host model libraries out of firmware target dependencies.
039. Keep optional Ethernet fixtures conditional on their module.
040. Keep optional web fixtures conditional on their module.
041. Keep native tooling verification in host CTest.
042. Add a host-sync profile to exercise synchronization-enabled scheduler layout.
043. Initialize both base and effective priorities in scheduler fixtures when enabled.
044. Compile scheduler wait-expiration support with matching host option definitions.
045. Explicitly disable logging in nano even when reusing a CMake cache.
046. Retain two user-facing service switches rather than adding per-manager controls now.
047. Retain independent board and feature profiles rather than duplicating target matrices.
048. Retain Native AOT tooling and C# XDoc automation unchanged.
049. Verify both ordinary and synchronization-enabled host configurations.
050. Rebuild firmware and documentation after the build-rule consolidation before commit.
