ARM simulator stress test
=========================

Scope
-----

The ``arm-stress`` preset builds the actual C++ kernel and a dedicated Cortex-M3
port for QEMU's ``mps2-an385`` machine. This executes ARM instructions, exception
entry/return, real thread stacks and blocking APIs. It is separate from the
host synchronization state-machine fixtures. VORAGO, Zynq and other ARM hardware
profiles remain planned; this simulation BSP does not qualify those devices.

The test has nine permanent application threads plus private idle. Two producers
each send 10,000 copied messages through an eight-slot FIFO. The consumer checks
each producer's sequence and independent arithmetic state. Two more threads
exchange a semaphore token for 20,000 total handoffs. Contended mutexes protect
shared operation counts, with explicit yields while holding ownership to exercise
blocked waiters. The test also checks:

* Queue full/empty behavior, FIFO conservation and timed-out blocked send/receive.
* Semaphore timeout, maximum/overflow behavior and final token conservation.
* Event timeout, sticky notifications and wait-all completion.
* A low-priority owner inheriting priority 4 while an intermediate-priority
  CPU-bound thread is ready. A missing donation causes a finite failure.
* A CPU-bound thread that does not yield during the main load, requiring timer
  slicing for its same-priority IPC peers to progress.
* Thread completion, stack-bottom sentinels, dispatch counts and timer progress.

The source fixes the default iteration count in ``RTOS_STRESS_ROUNDS``. It is a
finite stress campaign, not exhaustive interleaving exploration. No external
peripheral ISR dispatch, SMP, FP state, TLS, MPU isolation or worst-case latency
claim is made. Sentinels detect some corruption; they do not prove stack capacity.
Queued logging has separate host/RISCue checks; this ARM campaign reports through
semihosting rather than testing a UART or the logger.
The overall guest completion deadline is ``8 * RTOS_STRESS_ROUNDS + 1000`` ticks,
allowing CPU-load slices between IPC operations; individual blocking operations
retain their own finite deadlines.

Build and run
-------------

Install GNU ``arm-none-eabi`` C++/binutils, QEMU system ARM, CMake/Ninja and the
normal native .NET tool prerequisites. No newlib library is linked; the compiler
still needs standard freestanding type headers. On Ubuntu, compiler/header
packages include ``gcc-arm-none-eabi`` and ``libnewlib-dev``; the simulator is
``qemu-system-arm``. Use a Linux/WSL shell in the repository root::

   cmake --preset host -DRTOS_ARM_SIMULATION=ON
   cmake --build --preset host --target native_tools
   cmake --preset arm-stress \
     -DARM_TOOLCHAIN_PREFIX=/path/to/bin/arm-none-eabi- \
     -DNXR_QEMU_ARM_EXECUTABLE=/path/to/qemu-system-arm \
     -DRTOS_PREBUILT_NATIVE_TOOLS=build/host/native-tools
   cmake --build --preset arm-stress --target arm_stress

The ``arm-stress-no-watermark`` and ``arm-stress-no-guards`` presets exercise
the public diagnostic branches with instrumentation or guards disabled. Configure
them with the same toolchain, simulator and native-tool bundle settings.
The workload checks real BASEPRI restoration, including a stronger mask retained
across a blocking wait, plus boot and live-thread stack observations.
``static_contract_check`` also runs ARM positive/negative compiler-linker fixtures.

``arm_sim_tool`` publishes ``NexRT.ArmSim`` as a native host executable with XDoc.
The firmware link still runs the required allocator/constructor symbol audit.
The runner executes QEMU three times with instruction-clock shifts 3, 4 and 5,
varying virtual timer delivery relative to the same workload. Each subprocess has
a 120-second wall-clock bound. Timeout fails the run and attempts to kill the
owned process tree while its parent is alive. Orphans whose parent already
exited may outlive the runner; pipe cancellation still bounds the runner's wait.
Output is captured into bounded logs without synchronous console forwarding,
so an unread outer console pipe cannot block capture cancellation.
The deadline includes redirected-pipe draining, with at most two seconds of
bounded drain cleanup. Retained output is capped at eight million characters.
The native ``NexRT.ArmSim --self-test`` command covers normal/nonzero exits,
execution timeout, output flooding and descendant-held pipes; CTest runs it when
the native ARM runner is enabled. A descendant retaining a pipe after its parent
exits cannot keep the runner waiting beyond its deadline.
Success requires zero QEMU exit status and the exact guest ``ARM_STRESS_PASS``
marker. Logs, QEMU version and an ELF hash are retained beneath
``build/arm-stress/runs/<UTC timestamp>``. ``PASS.txt`` exists only after all three
runs pass. Host allocations belong to the runner/emulator, not the RTOS image.

Recorded campaign
-----------------

Run ``20261020-042440-422`` passed all three clock shifts using QEMU 8.2.2 and
GNU ARM Embedded GCC 13.2.1. Every run verified 20,000 messages, 20,000 token
handoffs, priority donation, finite timeout cases and all eight worker exits.
Each ended at tick 20,147 with intact stack sentinels and nonzero dispatch counts.
The CPU-load loop completed 5,732,888, 2,812,790 and 1,352,741 batches for shifts
3, 4 and 5 respectively, demonstrating different virtual interrupt placements.
The ELF SHA-256 was
``93007f748afc1c3685898e310a2a8fb75d1b0cc26372be7ecc9cfdd83468c924``.

The link reported 9292 bytes of code/constants, 304 bytes of initialized data
and 20,736 bytes of BSS. The separate exception/boot reserve is 4096 bytes.
This deliberately oversized nine-worker stress image is not the minimal kernel
footprint. These section totals do not include any linker alignment gaps.
The allocator/constructor audit passed. The 18 existing host checks and the
RISCue UART firmware rebuild also passed during integration.

Port and assembly contract
--------------------------

The BSP uses QEMU AN385's 25 MHz clock, CMSDK timer zero as a free-running time
source and SysTick for scheduling deadlines. Time reads extend the 32-bit timer
under the kernel mask; more than one complete counter wrap between reads is
unsupported. ``-icount`` timing is a virtual model, not a real execution-time
measurement. Code occupies a bounded 256 KiB region at zero; data/BSS and a
4096-byte exception/boot stack use a bounded 64 KiB region at ``0x20000000``.
These are subsets of the QEMU SSRAM map, not VORAGO addresses.
Near-due SysTick deadlines use a minimum two-cycle interval so a zero reload
cannot disable the timer; the shared kernel accounts late elapsed ticks.

Applications execute privileged on PSP; handlers use the separate MSP stack.
Hardware stacks r0-r3, r12, lr, pc and xPSR. Assembly adds BASEPRI, EXC_RETURN
and r4-r11, producing an 18-word/72-byte saved frame. The initial frame selects
Thumb state and ``0xfffffffd`` exception return to thread mode on PSP.
SVC's stacked PC already follows the instruction; no manual PC increment is used.

This port uses synchronous SVC for yielding/blocking, and switches directly from
SysTick for preemption. PendSV is unused. SVC priority zero stays callable while
BASEPRI masks priorities 0x80 and below, including SysTick. That preserves the
kernel's atomic enqueue-and-park contract. The prior mask is saved per thread
and restored before exception return. PRIMASK must remain clear during kernel
calls; applications must not change exception priorities or enable unrelated
interrupts. Cortex-M0 lacks BASEPRI and needs a different reviewed port.

Reset initializes BSS and calls ``main``. QEMU's ELF loader places initialized
data at its linked address; there is no flash-data copying or constructor runner.
Unexpected exceptions fail through the semihosting sink. Semihosting requires
the simulator; it is not a production console, fault handler or reboot policy.

References: `QEMU MPS2 machines <https://www.qemu.org/docs/master/system/arm/mps2.html>`_,
`QEMU AN385 model <https://github.com/qemu/qemu/blob/v8.2.2/hw/arm/mps2.c>`_,
`CMSIS register contracts <https://arm-software.github.io/CMSIS_6/main/Core/group__Core__Register__gr.html>`_
and the `Arm Cortex-M3 guide <https://documentation-service.arm.com/static/5ea823e69931941038df1af5>`_.

.. literalinclude:: ../ports/arm-mps2/context.S
   :language: asm
   :caption: Software frame save, scheduler call and exception return

.. literalinclude:: ../ports/arm-mps2/start.S
   :language: text
   :caption: Vector table and simulation startup
