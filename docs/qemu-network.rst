Host-reachable static UDP on QEMU
================================================================================

Design and implementation plan
--------------------------------------------------------------------------------

Run the actual RV32 NexRT kernel on QEMU 8.2.2's ``virt`` machine, with a
modern virtio-MMIO network device and unprivileged user networking. Forward
only a loopback host UDP port to static guest address 10.0.2.15:7. The existing
RISCue hardware profile and its instruction-model tests remain separate.

Reuse the integer RV32 context, startup and CLINT HAL, with a distinct linker
map at 0x80000000 and a separately identified QEMU target. Poll a fixed eight-
descriptor RX ring and one outstanding copied TX frame. Accept only modern
split-ring virtio, MAC configuration and VERSION_1; do not negotiate offloads,
merged receive buffers, indirect descriptors or packed rings. Every buffer and
queue lives in boot-owned storage; the synchronous MAC adapter never lends an
application pointer to the device. Guest RAM is flat and coherent in this QEMU
profile. This does not establish physical DMA/cache behavior or external IRQs.

Make IPv4 addressing an explicit typed UDP initialization setting, retaining
the existing 192.0.2.2 fixture defaults. QEMU uses 10.0.2.15/24, gateway10.0.2.2
and MAC02:00:00:00:00:02. A single network worker owns lwIP and the driver.
A peer worker and timer provide liveness evidence. No general heap is enabled.

The C#/.NET10 Native AOT host client launches an isolated QEMU process, waits
for a bounded readiness signal, validates seeded UDP byte-for-byte echoes and
oversize rejection/recovery, and always observes cleanup. A separate probe
operation can test a running endpoint. Host socket buffers are bounded; the
host process is not subject to the target's no-heap rule.

For littlefs, strengthen the existing NexRT-owned port rather than fork the
filesystem engine: enforce LFS_NO_MALLOC at the adapter boundary and reserve
the file control block/configuration as well as all caches permanently inside
each volume. Test the zero-allocation fallback and fixed single-file capacity.

Evaluate Boost static_vector, StaticString and Intrusive separately, recording
actual allocation, error and lifetime behavior. Do not adopt a new dependency
without demonstrated fit; do not add Boost to firmware by default.

Verification sequence
--------------------------------------------------------------------------------

1. Add failing driver, IPv4 configuration and static-file-slot tests.
2. Implement the bounded driver and QEMU target; validate with a host MMIO model.
3. Run host-to-guest echoes through QEMU host forwarding and a real host socket.
4. Test missing devices, malformed completions, full rings, zero/max/oversize
   datagrams, timeouts, host cancellation/cleanup and stripped configurations.
5. Run regressions, sanitizers, seeded fuzz/property checks, targeted mutations,
   line/branch coverage, real StrictDoc and Sphinx; review before commit/push.

Reuse assessment
--------------------------------------------------------------------------------

QEMU's documented virt board supplies CLINT and eight virtio-MMIO transports;
its user network backend avoids privileged TAP setup. The current Ethernet
adapter and lwIP integration already provide the stack boundary. The Rust
``rcore-os/virtio-drivers`` library is a useful protocol reference but introduces
a second guest-language toolchain and allocator-oriented network ownership.
For this narrow single-owner, fixed-ring C++ profile, a small transport adapter
is the domain-specific missing integration. The OASIS Virtio1.2 specification
defines the handshake, ownership and ordering; actual QEMU traffic is the
independent execution reference. No source is copied from another driver.

Sources: https://www.qemu.org/docs/master/system/riscv/virt.html,
https://www.qemu.org/docs/master/system/devices/net.html,
https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html,
https://github.com/rcore-os/virtio-drivers.

Boost evaluation decision
--------------------------------------------------------------------------------

The bounded host proof uses Boost 1.83.0 (Ubuntu package
``libboost1.83-dev 1.83.0-2.1ubuntu3.2``), C++20 and GCC -O3. Successful
``static_vector<int,2>``, ``static_string<8>`` and caller-owned intrusive list
operations made zero intercepted ordinary scalar ``operator new`` calls.
Overflow preserved sizes and threw ``boost::container::bad_alloc`` and
``std::length_error`` respectively. This intercept does not cover malloc,
array/aligned allocation or exception-runtime allocation. Intrusive hooks do
not own object lifetimes, identify owning containers, or provide synchronization.

A separate global-object fixture emitted ``.init_array`` and ``__cxa_atexit``
for static_vector; constexpr literal StaticString succeeded. Turning exceptions
off does not turn overflow into a recoverable result. Therefore retain the
existing explicit full-result RTOS queues and ownership helpers. Boost is an
optional host evaluation, not a target dependency. ``std::span`` remains the
preferred non-owning view; Boost.Compat function_ref was researched but is absent
from the tested1.83 installation and has not been executed.

Reproduce with ``RTOS_BOOST_EVALUATION=ON`` and ``NXR_BOOST_INCLUDE`` pointing to
an existing Boost include directory. The reusable fixture is
``tests/boost_static_evaluation.cpp``. Boost uses the Boost Software License1.0;
no Boost source is redistributed by this change. Primary references:
https://www.boost.org/doc/libs/1_83_0/doc/html/container/non_standard_containers.html,
https://www.boost.org/doc/libs/1_83_0/doc/html/intrusive.html,
https://www.boost.org/doc/libs/1_83_0/libs/static_string/doc/html/index.html.

Littlefs ownership and recovery
--------------------------------------------------------------------------------

NexRT owns the port, volume caches, one file-control slot and its configuration;
upstream littlefs remains pinned and unmodified. The adapter refuses compilation
without ``LFS_NO_MALLOC``. Operations reject callback reentry, but this guard is
not thread synchronization: one worker owns a volume. After a media I/O error,
restore the device and unmount/remount before retrying; immediate same-mount
retry was observed to encounter cached metadata after an injected read failure.
The port does not promise recovery from arbitrary physical power interruption.

Build and run
--------------------------------------------------------------------------------

On the qualified Ubuntu24.04 environment with pinned lwIP already available::

   cmake --preset qemu-udp -DFETCHCONTENT_SOURCE_DIR_LWIP=/absolute/path/to/lwip
   cmake --build --preset qemu-udp --target demo -j8
   cmake --build build/host --target network_test_tool -j4
   build/host/native-tools/NexRT.NetTest/NexRT.NetTest smoke build/qemu-udp/demo.elf /existing/artifact/parent/new-run 10007

Check directories before using these paths. ``smoke`` creates the named run
subdirectory only when its parent exists. The client owns and stops its QEMU
child; ``probe HOST PORT COUNT`` attaches without owning the server. Forwarding
binds127.0.0.1 only. The current profile is single-hart RV32 M-mode, not an ARM
or physical RISCue board qualification. ``RTOS_TASK_TRACE=OFF`` removes recording
and the inversion witness without disabling UDP; ``RTOS_VIRTIO_NET=OFF`` removes
the network driver from other applications. The UDP demo requires that driver.

Trace wire contract
--------------------------------------------------------------------------------

NXT1 frames contain32 decoded bytes, COBS encoded into33 bytes followed by zero.
Fields are little endian: magic NXT1 at0..3, version1 at4, kind at5, thread and
related IDs at6..7, base/effective priorities at8..9, flags at10..11, sequence at
12..15, timer count at16..23, value at24..27, CRC-32C over bytes0..27 at28..31.
CRC uses reflected Castagnoli polynomial0x82f63b78 with initial/final all ones.
Normal timestamps are the CLINT counter, not instruction counts or logical
scheduling ticks. Clock metadata(kind129) uses its time field as counter Hz;
thread metadata(kind128) packs12 printable name bytes into time/value; loss
metadata(kind130) uses value as cumulative dropped events. ID255 identifies idle
or unknown related thread. A future extension must version this format.

Kernel hooks mask local interrupts briefly, copy a fixed record, and return.
No hook formats text or touches the UART. One worker drains at most272 wire
bytes per call; partial writes retain frame progress. Full ring drops new
records, advances sequence and eventually emits cumulative loss after draining.
The default512-record ring is fixed; host output speed does not impose waits
inside hooks. This is diagnostic instrumentation: its execution and UART drain
cost affect scheduling and have not been qualified as a real-time bound.
The QEMU demonstration shares its sole16550 with a finite ASCII readiness prefix,
then emits an initial zero and only binary trace. A production board should use
a dedicated monitor UART or another independently budgeted transport.

UART capacity planning
--------------------------------------------------------------------------------

A34-byte frame on8-N-1 consumes340 serial bits. At115200 baud the theoretical
ceiling is about338 events/second;1Mbaud permits about2941 and3Mbaud about8823,
before other traffic and implementation overhead. A1kHz scheduler can exceed
the115200 link easily. QEMU's stdio UART does not qualify real baud-rate limits.
For a board, start with a dedicated1--3Mbaud monitor UART or USB serial endpoint,
measure actual event rates and recorder/drain cost, then size retention for
bursts. The default512 records consume roughly12KiB on this RV32 build. Keep
recording optional and tolerate explicit loss; do not block the scheduler to
preserve a trace. USB endpoint framing must not silently rely on USB transfer
boundaries: retain the version/CRC/sequence protocol across arbitrary chunks.
