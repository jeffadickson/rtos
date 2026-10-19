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

Checkpoint 75: logger implementation and output behavior
--------------------------------------------------------------------------------

051. Keep private formatting followed by a copied queue record.
052. Keep drop-new publication rather than adding blocking logger modes.
053. Keep one owned drain path to preserve complete-record ordering.
054. Separate staging a pending record from emitting bytes to a writer.
055. Update pending state, offset and completion counters in one critical section.
056. Reset the reported byte offset when a record completes.
057. Return empty only when no output was available and no progress occurred.
058. Return ok after successfully draining all available output.
059. Retain busy for real backpressure or remaining queued output.
060. Retain pending bytes after negative writer errors.
061. Retain rejection of callbacks claiming more bytes than offered.
062. Keep explicit byte and loop budgets for each drain.
063. Keep UART callbacks outside interrupt-masked critical sections.
064. Check recursive drain attempts fail promptly through nonrecursive ownership.
065. Centralize formatting in vprintf so application wrappers do not duplicate it.
066. Copy va_list state so forwarding does not consume caller state.
067. Add GCC/Clang format type checking to variadic printf entry points.
068. Retain runtime rejection of unsupported format features.
069. Reduce numeric scratch from 64 bytes to the 20 decimal digits required by uint64.
070. Compute quotient once and derive remainder rather than requesting two 64-bit divisions.
071. Retain unsigned arithmetic for the minimum signed integer.
072. Avoid rescheduling on an uncontended unlock with unchanged effective priority.
073. Retain rescheduling when a waiter is granted ownership or donation changes.
074. Exercise forwarding, callback context, recursive drain and coherent completion state.
075. Recheck host behavior, target code/frame sizes and static analysis before commit.

Checkpoint 100: a short starting path and final verification
--------------------------------------------------------------------------------

076. Lead the manual with a short application guide rather than the full manager reference.
077. Teach one convenience include before individual implementation headers.
078. Start application communication with a copied typed value queue.
079. Make immediate full/empty results explicit in the first queue example.
080. Label timeout values as ticks in the first receive example.
081. Explain pointed-to storage ownership without introducing a buffer-loan API.
082. Teach whole device transactions as one scoped operation.
083. Explain that the transaction timeout covers lock waiting, while callbacks need their own operation bounds.
084. Teach console.printf with one permanent output thread and a synchronous callback.
085. Keep events, semaphores and atomic words in an optional next-step section.
086. Add a complete two-thread console example with a four-record static logger.
087. Mark the counting output sink explicitly rather than inventing UART addresses.
088. Mark the example timer clock as a board integration input.
089. Keep a single comprehensive integration example alongside the small starting example.
090. Remove redundant component includes from the integration example.
091. Document vprintf forwarding, compiler format checks and drain result meanings.
092. Preserve the original section-size table as historical measurement evidence.
093. Record current code and numeric-frame measurements without claiming a safe whole-thread stack bound.
094. Add the internal wait header to source traceability rather than hiding it from review.
095. Keep requirements marked source-unverified pending target qualification.
096. Normalize newly authored text and check the staged diff.
097. Compile and audit the small console example independently of the integration example.
098. Recheck all ordinary and synchronization-enabled host fixtures.
099. Recheck nano, synchronization, logging, baseline firmware, static contracts and Clang analysis.
100. Build the Sphinx manual, verify the 100-entry review log, commit locally and push only after final gates pass.

Measured refinement results
--------------------------------------------------------------------------------

Compared with the original synchronization implementation pass, the same full
logging example changed from 11072 code/read-only + 860 initialized + 3504
zero-fill bytes to 10192 + 860 + 3520 bytes: 864 fewer allocated section bytes.
The Logger<8,64> object remains 720 bytes; this reduction is primarily code.
The compiler-reported Buffer::number frame changed from 112 to 80 bytes, while
format_log's own frame remains 96 bytes. These reports do not include the entire
call chain, trap frame or interrupt nesting and do not qualify thread stack size.
Nano remains 3648 code/read-only + 1152 zero-fill, plus the 4096-byte boot reserve.
