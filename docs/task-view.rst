NexRT Task View
================================================================================

Task View is an initial native C#/.NET 10 Avalonia desktop viewer for optional
NexRT instrumentation. The empty window contains instructions rather than sample
activity. Use the capture path field and Open capture, or a QEMU UART TCP
endpoint and Connect UART TCP. Disconnect cancels the active asynchronous read.
Each new source starts a new viewing session. The CLI uses the same typed source
commands as the buttons::

   dotnet run --project App -- --trace capture.bin
   dotnet run --project App -- --serial localhost:5556

Run these commands from the existing tools/NexRT.TaskView directory. On Windows, verify the
directory with ``Test-Path -LiteralPath $repo -PathType Container`` before using
``Set-Location -LiteralPath $repo``. The serial option refers to a TCP bridge to
QEMU's shared console UART, not the QEMU command monitor.
Physical COM/tty support is not yet implemented. No target commands are sent. For a manual QEMU launch replace ``-serial stdio`` with ``-serial tcp:127.0.0.1:5556,server=on,wait=on`` and use ``-monitor none``. Connecting the viewer releases the waiting guest. The single 16550 UART at 0x10000000 first emits ASCII boot readiness, then a zero delimiter and framed trace; no further console text is emitted. Runtime events are exported only by the instrumentation worker. The initial ASCII prefix is reported as one rejected frame.

Wire contract and display
--------------------------------------------------------------------------------

Frames are COBS encoded, terminated by zero, and decode to exactly 32 bytes.
Offsets 0..3 are NXT1; byte 4 is version 1; byte 5 is event kind; bytes 6/7 are
thread/related ID; bytes 8/9 base/effective priority; bytes 10..11 flags; bytes
12..15 sequence; bytes 16..23 timestamp; bytes 24..27 value; bytes 28..31 CRC32C.
Multibyte numbers are little endian. CRC covers bytes 0..27, with reflected
polynomial 0x82f63b78 and all-ones initial/final XOR. ClockInfo (kind 129) stores the CLINT timebase frequency in its timestamp field (10,000,000 Hz on the tested virt machine). Loss (kind 130) stores cumulative target drops in its value field. These metadata records count toward sequence continuity but are excluded from task lanes. ThreadInfo (kind 128) stores
a zero-padded UTF-8 name in bytes 16..27 instead of timestamp/value.

Dispatch, Ready, Block, Priority, MutexWait, MutexAcquire, MutexRelease, Finish,
and Sleep have IDs 1 through 9. Higher numerical priority is more urgent in NexRT. Task
lanes remain at stable ID positions while priority changes appear as markers.
Mutex wait markers connect waiter and owner lanes; click a marker for exact
priority, related-task, sequence, flags and value evidence. Base/effective
priority differences expose inheritance activity. They do not independently
prove an inversion or its duration. Timestamp units are CLINT timer counts; no clock
frequency is inferred. Dispatch intervals end at a subsequent dispatch or the
running task's block/sleep/finish event. Open-ended intervals remain markers.

Rejected frames, sequence discontinuities (including restarts), and evictions
are reported. Oversized frames are discarded through their next delimiter.
Retention is bounded at 100,000 records, and rendering uses the latest 4,000
nonmetadata events. Missing events can invalidate state-duration interpretations;
interval derivation preserves ordered metadata and refuses to bridge a sequence gap, loss record, rejected-frame boundary or backward target timestamp. Such activity remains a marker with an unknown interval in the inspector; this viewer does not reconstruct or certify missing activity. The zoom slider expands the timeline up to 40 times; use the horizontal scrollbar to inspect short events. There is no
search, timestamp calibration, automated inversion classification, export,
physical serial adapter or MCP command adapter yet. Keyboard access to the
native toolbar is provided by Avalonia; timeline marker selection is currently
pointer based. Linux and Native AOT runtime qualification remain outstanding.

Reuse assessment and provenance
--------------------------------------------------------------------------------

Research on 2026-11-18 consulted the private GUI repository's AGENTS.md, Control
Atlas README, native registry, component research and assessment guidance. The
registry's suggestion and filter controls do not meet task timeline semantics.
Avalonia Desktop and Fluent Theme 12.1.6 are pinned native dependencies (MIT
upstream); standard text, buttons, panels and scrolling are reused. A small
application-specific renderer uses Avalonia DrawingContext; no third-party
control source, icons, fonts, graphics, or vendor product artwork were copied.
No new chart package is needed for bounded task lanes. This is an independent
application view, not a reusable GUI catalog contribution or vendor-compatible
viewer. Primary Avalonia guidance:
https://docs.avaloniaui.net/docs/custom-controls/custom-drawn-controls

Wind River's own migration paper describes System Viewer as visualizing
interactions among interrupts, OS primitives and application tasks/threads:
https://www.windriver.com/resource/enabling-the-migration-to-software-defined-platforms-for-critical-infrastructure
Its historical System Viewer User's Guide 4.9 is indexed at Bitsavers but the
fetch returned HTTP 403 in this session; it was not execution-validated:
https://www.bitsavers.org/pdf/windRiver/Workbench_2.6.1_2007/wr_system_viewer_users_guide_4.9.pdf
The WindView concept informs stable task lanes and priority markers. No VxWorks
wire protocol or proprietary implementation is reused.

Zephyr's primary tracing documentation separates event format from transport,
supports CTF and custom formats, and lists UART, USB, file, RTT and RAM backends.
Its SystemView integration specifically uses RTT; SystemView UART capability
must not be mistaken for Zephyr's implementation support. CTF/Trace Compass
export is a promising later reuse path, but is not implemented in Task View:
https://docs.zephyrproject.org/latest/services/tracing/index.html

Verification
--------------------------------------------------------------------------------

Build and tests from tools/NexRT.TaskView::

   dotnet build App -p:PublishAot=false
   dotnet test Tests --collect:"XPlat Code Coverage" --results-directory "$artifactDirectory" -p:PublishAot=false

Verify the artifact parent directory exists before creating a task-specific
artifact directory. Keep Windows and Linux outputs separate. Tests cover an
independent standard CRC vector, every chunk split of a valid frame, every
single-bit encoded-frame corruption, oversized resynchronization, sequence wrap,
retention, missing files and malformed endpoints. Random malformed-input seed
is 741923, 5,000 bounded chunks. This is a reproducible custom robustness check,
not an established coverage-guided fuzzer or a formal proof. Headless Avalonia
renders both the actual empty window and decoded event lane. Optional screenshot
path: NEXRT_TASKVIEW_SCREENSHOT. GUI interaction, live target, Linux and hardware
coverage must be reported separately from core parser coverage.

The TaskView-local ``global.json`` selects the installed .NET 10.0.4xx SDK
with patch roll-forward. Invoke dotnet from ``tools/NexRT.TaskView`` for that
pin to take effect (SDK resolution starts at the working directory). The initial
Windows exploratory build used SDK 11.0.100 against net10.0; final evidence must
identify the actual selected SDK. A pinned local Stryker tool manifest supports
``dotnet tool restore`` from this directory. From Tests, run::

   dotnet stryker --config-file stryker-config.json --output "$artifactDirectory/mutation"

The mutation scope is Core/Trace.cs, with four workers and an additional ten
second test timeout. GUI code is outside this mutation scope. Mutation tests
and normal builds/tests share outputs and must run sequentially.





Windows verification, 2026-11-18
--------------------------------------------------------------------------------

The final Windows .NET 10.0.404 build passed with zero warnings/errors.
NUnit 4.5.1, adapter 5.1.0, Microsoft.NET.Test.Sdk 17.14.1, and Coverlet
collector 6.0.4 were used. The real QEMU capture contains 2,044 validated
records, one rejected ASCII prefix, no sequence discontinuities, one priority
inheritance event and one mutex wait. Clock metadata reports 10 MHz. Target loss
is shown as unknown until a Loss record is received; absence of metadata is not
silently displayed as zero. Both the full capture and 20x startup zoom were
rendered and visually inspected. The startup zoom shows low-owner base/effective
priority 1/4, the high-waiter 4/4 dependency, and medium-priority activity.

An earlier Core-only Stryker.NET 5.1.0 campaign on the pre-clock/loss source,
using SDK 11.0.100 targeting net10.0, completed: 144 generated, 89 killed,
19 survived, two timeout kills, 14 compile errors, three uncovered, 17 ignored;
reported mutation score 80.53%. Surviving areas included metadata, diagnostics,
endpoint boundaries and equivalent unsigned shifts. Metadata/diagnostic schema
tests were strengthened afterward. A repeat under SDK 10 stalled without a
report and was cancelled; a Core-test-filtered repeat failed initial test
execution. Cause is unresolved. The earlier score is historical evidence,
not a score for the final source or the new clock/loss and interval-derivation paths. No mutation
qualification claim is made. The final .NET 10 normal test/coverage run remains
separate. See tools/NexRT.TaskView/verification.txt for exact final evidence.
