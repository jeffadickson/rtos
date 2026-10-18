Integration history
===================

These are individual integration changes, grouped into five25-item batches.
They are not125 complete kernel implementations or human reviews.

1. **Native integration workbench** — Added a native-safe C# command surface for integration, content and local history. Source: ``tools/NexRT.Workbench/Program.cs``, anchor ``public static async Task<int> Main``.

2. **Detached commit launch** — Starts a detached worker and returns its PID without waiting for Git commits. Source: ``tools/NexRT.Workbench/Program.cs``, anchor ``private static void StartWorker``.

3. **Immutable commit snapshots** — Copies source bytes before queuing so later edits cannot enter earlier commits. Source: ``tools/NexRT.Workbench/Program.cs``, anchor ``private static async Task Queue``.

4. **Serialized private-index commits** — Builds local Git trees from saved bytes with a private index and serialized HEAD updates. Source: ``tools/NexRT.Workbench/Program.cs``, anchor ``private static async Task Worker``.

5. **Exclusive commit worker** — Prevents concurrent workers from racing on the local branch and queue. Source: ``tools/NexRT.Workbench/Program.cs``, anchor ``using var workerLock``.

6. **Bounded process execution** — Uses argument lists, child timeouts and captured diagnostics without shell interpolation. Source: ``tools/NexRT.Workbench/Program.cs``, anchor ``private static async Task<string> Run``.

7. **Source-backed integration ledger** — Requires25 distinct changes with existing source anchors and content hashes. Source: ``tools/NexRT.Workbench/Program.cs``, anchor ``private static void Record``.

8. **Checkpoint attempt retention** — Executes requested25-step gates and retains failed and successful timestamped attempts. Source: ``tools/NexRT.Workbench/Program.cs``, anchor ``private static async Task CheckpointRun``.

9. **ELF section accounting** — Reads bounded ELF32 section tables and excludes container/debug bytes from image size. Source: ``tools/NexRT.Workbench/Program.cs``, anchor ``private static void Measure``.

10. **RAM-resident code budgeting** — Counts code, data, BSS, alignment and the separate4096-byte boot reserve. Source: ``tools/NexRT.Workbench/Program.cs``, anchor ``Total committed RISCue RAM``.

11. **Measured HTML generation** — Builds the introduction using measured byte counts and the exact firmware digest. Source: ``tools/NexRT.Workbench/Program.cs``, anchor ``private static void Site``.

12. **Immutable content compiler** — Compiles bounded HTML into static C++ byte arrays and records the source digest. Source: ``tools/NexRT.Workbench/Program.cs``, anchor ``private static void Assets``.

13. **Loopback page preview** — Provides a local host preview independent of an unimplemented board Ethernet stack. Source: ``tools/NexRT.Workbench/Program.cs``, anchor ``private static async Task Serve``.

14. **One-thread nano application** — Introduces a real minimal application with one512-byte worker stack and no diagnostics buffers. Source: ``examples/nano.cpp``, anchor ``extern "C" int main``.

15. **Independent nano build profile** — Disables optional services and sets a512-byte idle stack and one application slot. Source: ``CMakePresets.json``, anchor ``"name":"nano"``.

16. **Selectable firmware application** — Allows independent example images without editing the firmware target source list. Source: ``CMakeLists.txt``, anchor ``set(RTOS_APPLICATION``.

17. **Validated native host bundle** — Rejects prebuilt tools whose host identity or source digest differs from current inputs. Source: ``cmake/host-tools.cmake``, anchor ``Stale or wrong-host native tool``.

18. **Read-only byte views** — Adds explicit pointer-length views with null/nonempty validation for transport and asset input. Source: ``include/rtos_static.hpp``, anchor ``struct Bytes``.

19. **Writable byte views** — Separates destination capacity from immutable source buffers without allocating. Source: ``include/rtos_static.hpp``, anchor ``struct MutableBytes``.

20. **Checked size arithmetic** — Rejects overflowing lengths while preserving the caller's result on failure. Source: ``include/rtos_static.hpp``, anchor ``inline bool checked_add``.

21. **Saturating health counters** — Keeps resource-pressure counters from wrapping silently. Source: ``include/rtos_static.hpp``, anchor ``inline void saturate``.

22. **Bounded ring insertion** — Adds drop-newest backpressure and a saturating exhaustion count to fixed storage. Source: ``include/rtos_static.hpp``, anchor ``Result push``.

23. **FIFO ring consumption** — Consumes fixed ring slots in FIFO order across wrap without changing output on empty. Source: ``include/rtos_static.hpp``, anchor ``Result pop``.

24. **Generation-tagged static loans** — Prevents stale release and retires exhausted generations without heap fallback. Source: ``include/rtos_static.hpp``, anchor ``Result acquire``.

25. **Quarantined pool ownership** — Removes unsafe loans from reuse and exposes conservation checks. Source: ``include/rtos_static.hpp``, anchor ``Result quarantine``.

