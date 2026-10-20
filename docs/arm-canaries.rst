ARM register and timer validation
================================================================================

The stress application calls three assembly harnesses. Ordinary applications
do not reference them, so archive selection and section garbage collection
remove them. They allocate only on the calling thread's static stack.
These checks exercise the QEMU Cortex-M3 port; they do not qualify another MCU.

Register contracts
--------------------------------------------------------------------------------

``rtos_arm_callee_canary(inject)`` saves the caller's r4-r11 and LR (36 bytes)
and reserves four bytes for an injection flag, keeping an eight-byte-aligned
40-byte frame. Distinct r4-r11 constants survive SVC yield and a C++ helper
that sleeps for two ticks. It compares each register, then restores the caller.
It deliberately makes no claim about caller-clobbered registers across C calls.

``rtos_arm_irq_canary(inject)`` saves the same 36 bytes and reserves 76 bytes,
giving an aligned 112-byte total. Scratch offsets 0..55 contain r0-r12 and LR;
56 contains APSR, 60 the injection flag, and 64 the loop count. It repeatedly
seeds fourteen distinct registers, sets NZCV to 1000, executes 64 NOPs without
calling C, and snapshots registers and flags before comparisons alter them.
The application requires tick advancement across the positive 10,000-loop call
and a stress-only handler witness whose stacked PC falls in the half-open
``rtos_arm_irq_live_begin`` / ``rtos_arm_irq_live_end`` NOP region.
This covers interrupt-only preservation; it does not claim FP or Q-flag testing.

Both functions return one for intact state and zero for a mismatch. The
application separately requires zero when an r7 bit is intentionally flipped,
then one for a positive call. A separate input value of two injects incorrect
NZCV without changing a register, and must also return zero.
A guest crash, timeout or nonzero process exit
cannot substitute for successful fault detection.

Alignment contract
--------------------------------------------------------------------------------

``rtos_arm_padding_canary()`` saves r4-r6/LR (16 bytes), preserves BASEPRI and
masks SysTick. It subtracts four bytes from PSP, executes SVC, then reads the
hardware-stacked xPSR below the returned PSP before any C call can overwrite it.
It requires the alignment-padding flag and restoration of the original PSP.
There is no C ABI call on the deliberately misaligned stack. The port explicitly
enables CCR.STKALIGN, and handler C++ continues to execute on aligned MSP.

Timer variations
--------------------------------------------------------------------------------

The workload masks SysTick for five periods while reading the independent
CMSDK timer. The baseline is sampled after masking, avoiding a tick-phase race.
Logical ticks must stay fixed while masked. A stress-only hook records the
first post-mask timer handler's computed elapsed count, which must be at least
five. Repeated single-tick interrupts cannot satisfy that check. A peer already
blocked on a three-tick semaphore wait must be detached and resume with timeout.
The entire recovery has a twenty-period counter deadline.
This tests elapsed-period accounting rather than assuming every tick edge queues
an interrupt. It does not measure hardware interrupt latency.

``arm-stress-wrap`` starts the countdown at 256 but leaves RELOAD at UINT32_MAX.
Serialized observations require an initial low word near UINT32_MAX, a subsequent
extended-time epoch, and nondecreasing extended values. An ignored preload
therefore cannot satisfy the near-wrap test. Short repeated
reloads would invalidate the counter-extension modulus and are not used.
``arm-stress-fast`` uses 2000 Hz and a three-slot message queue to vary timing
and ring wrap. Configure these like ``arm-stress``, with the same compiler,
QEMU path and prebuilt native tools; build the respective ``arm_stress`` target.

``RTOS_ARM_STRESS_WITNESS`` defaults OFF and is ON in stress presets. It adds
observer calls and small validation state only to those images. The callbacks
do not block, call kernel APIs, or modify scheduling decisions. Ordinary target
builds retain neither these hooks nor their storage.

.. literalinclude:: ../ports/arm-mps2/canaries.S
   :language: text
   :caption: Validation-only assembly, with stack and register contracts
