# Kernel I/O Channel Design

Status: draft for review
Scope: single-CPU tiny OS kernel. Covers the kernel logging path (`printf`-style output) and a general-purpose channel abstraction for user I/O, including AXI-attached devices.

---

## 1. Goals

- User code can call `printf` and similar functions from any thread without corrupting output.
- No global lock on the formatting path. Formatting is private to the caller.
- A thread that crashes, is killed, or is preempted at any point must not deadlock the system or corrupt shared state.
- Interrupt handlers can submit log messages without sleeping or spinning on a slow device.
- The same mechanism can serve other user I/O, with extra rules where hardware (DMA, AXI bus) is involved.

## 2. Assumptions

- Single CPU. No concurrent execution of kernel code on two cores.
- Scheduler is not yet complete. Preemption and task-exit hooks are not available yet. See section 2.1 for the interim behavior.
- Interrupts can be disabled and restored (`irq_save` / `irq_restore`).
- Compiler barrier available (`asm volatile("" ::: "memory")`) or equivalent.
- Initial device scope is byte and stream peripherals only. DMA is out of scope for the first release.

If the system moves to multiple CPUs, the claim step needs atomic compare-and-swap, and the head index must become an atomic. See section 9.

### 2.1 Interim behavior without a scheduler

- There are no task IDs yet. Producers pass a constant `tid` (0). The `tid` field stays in the slot so the reaper can be added later without changing the layout.
- `log_reap` is not called yet. The reaper is deferred until the task-exit path exists.
- Without preemption, a producer can only be interrupted by an ISR, and the ISR claims a different slot. A slot can then stay in `WRITING` only if a fault interrupts the copy. The fault path must therefore record the slot being written (a single `current_claim` pointer, set in `klog` and cleared on publish) and mark it `ABORTED`.
- Drain runs from the idle loop only.
- When the scheduler lands, add the reaper call to the task-exit path and replace the constant `tid` with the real task ID. No other producer code changes.
- Byte and stream devices use the ring as described in section 4. Their drain paths should use TX-empty or FIFO-ready interrupts once the interrupt controller is working. Until then, the polled drain in section 4.2 is acceptable.

## 3. Architecture

### 3.1 Division of work

| Layer | Responsibility | Locks |
|---|---|---|
| User `printf` | Format into a thread-local buffer | None |
| User syscall `sys_log` / `sys_chan_send` | Hand a finished message to the kernel | None in user space |
| Kernel channel | Claim slot, copy message, publish | Interrupts disabled for the claim only |
| Consumer (drain) | Move published messages to hardware | None; never waits on a producer |
| Reaper (task exit) | Clean up slots left mid-write by a dead task | None; runs in task-exit context |

### 3.2 Why not a global lock

A single lock around the whole write has three problems:

1. A thread that dies while holding the lock deadlocks the system. The kernel has no owner record to release it.
2. Interrupt handlers that print must wait behind a slow UART.
3. Formatting work is serialized even though it touches only private data.

The slot ring removes all three. A crash costs at most one message.

### 3.3 Slot ring

A fixed array of slots, each with a state word:

```c
#define SLOTS   256
#define MSG_MAX 120

enum { S_FREE, S_WRITING, S_READY, S_ABORTED };

struct slot {
    uint8_t  state;     // volatile; changed only by the owner or the consumer
    uint8_t  tid;       // owning task ID
    uint16_t len;
    char     msg[MSG_MAX];
};

static struct slot ring[SLOTS];
static uint32_t head;        // producer index; touched only with interrupts off
static uint32_t tail;        // consumer index; consumer only
static uint32_t dropped;     // count of messages lost to a full ring or aborts
```

State transitions:

```
FREE --claim--> WRITING --publish--> READY --drain--> FREE
                   |
                   +--fault / reap--> ABORTED --drain--> FREE
```

## 4. Kernel logging implementation

### 4.1 Producer

```c
int klog(const char *src, size_t len, uint8_t tid, bool from_user)
{
    struct slot *s;
    irq_state_t flags = irq_save();

    s = &ring[head % SLOTS];
    if (s->state != S_FREE) {
        irq_restore(flags);
        dropped++;
        return -1;                  // ring full: drop, never block
    }
    s->state = S_WRITING;
    s->tid   = tid;
    head++;
    irq_restore(flags);

    s->len = len < MSG_MAX ? len : MSG_MAX;
    int err = from_user ? copy_from_user(s->msg, src, s->len)
                        : (memcpy(s->msg, src, s->len), 0);
    if (err) {
        compiler_barrier();
        s->state = S_ABORTED;
        return -EFAULT;
    }
    compiler_barrier();
    s->state = S_READY;            // publish last
    return s->len;
}
```

Notes:
- Interrupts are off only for the claim, a few instructions. The copy runs with interrupts enabled, so a long copy never delays an ISR.
- The state word is written after the message bytes. The consumer only reads slots in `S_READY`, so it never sees a torn message.
- A full ring drops the new message and increments `dropped`. Producers never block.

### 4.2 Consumer (drain)

```c
void log_drain(void)
{
    for (;;) {
        struct slot *s = &ring[tail % SLOTS];
        uint8_t st = s->state;     // volatile read

        if (st == S_READY)
            uart_write(s->msg, s->len);
        else if (st == S_ABORTED)
            ;                      // loss already counted; skip
        else
            break;                 // FREE or WRITING: stop, retry later

        compiler_barrier();
        s->state = S_FREE;
        tail++;
    }
}
```

Notes:
- Called from the idle loop or a low-priority thread. Never from an ISR, since a long UART write would stall everything.
- Stops at a `WRITING` slot instead of spinning. A preempted producer will finish; a dead one is reaped.
- Preferred long-term: drive the UART from its TX-empty interrupt, one byte at a time, so draining never blocks.

### 4.3 Reaper

```c
void log_reap(uint8_t tid)
{
    for (int i = 0; i < SLOTS; i++)
        if (ring[i].tid == tid && ring[i].state == S_WRITING)
            ring[i].state = S_ABORTED;
}
```

Called from the task-exit path. On one CPU, a task that dies mid-write is either the running task (handled by the copy fault path) or a preempted task (handled here). The scan is 256 iterations and runs once per task exit.

## 5. Crash and failure cases

| Event | Outcome |
|---|---|
| Thread crashes in user code while formatting | Nothing in the kernel is touched. Message lost, no other effect. |
| Bad user pointer, page fault in `copy_from_user` | Fault path sets slot to `ABORTED`. Consumer skips it, `dropped` is not incremented for this path (or increment it; pick one and document it). |
| Thread killed after claim, before publish | Task-exit path calls `log_reap`, slot becomes `ABORTED`. |
| Thread preempted between claim and publish | Slot stays `WRITING`. Consumer stops there and retries next drain. Thread finishes when rescheduled. |
| ISR logs while a thread holds a claimed slot | ISR claims a different slot. No conflict. |
| Ring full | Message dropped, `dropped` incremented, caller gets `-1`. |
| Consumer falls behind | Same as ring full. Drop, never block. |

No case in this table requires a lock held by a dead thread, so none can deadlock.

## 6. General-purpose channels

Logging is one instance of a more general channel. Parameterize the ring:

```c
enum drop_policy { DROP_NEW, DROP_OLD, BLOCK_CALLER };

struct chan_ops {
    int  (*submit)(void *dev, const void *req, size_t len);   // program hardware or enqueue
    void (*complete)(void *dev, struct slot *s);              // called from IRQ or drain
    void (*abort)(void *dev, struct slot *s);                 // undo a half-done request
};

struct channel {
    struct slot         *ring;
    uint32_t             depth;
    uint32_t             msg_max;
    enum drop_policy     policy;
    const struct chan_ops *ops;
    void                *dev;
};

int  chan_send(struct channel *c, const void *src, size_t len, uint8_t owner);
void chan_drain(struct channel *c);
void chan_reap(struct channel *c, uint8_t owner);
```

- `klog` is a channel with a UART `ops` table and `DROP_NEW`.
- `BLOCK_CALLER` is allowed only for channels used from task context. It must never be used from an ISR.

### 6.1 Slot states for device requests

Logging uses four states. Device requests need more, because the hardware holds the request after the software is done with it:

| State | Meaning | Who may change it |
|---|---|---|
| `SUBMITTED` | In the ring, not yet given to hardware | Owner (before submit), consumer |
| `IN_FLIGHT` | Owned by hardware; memory must not be freed | IRQ handler, abort path |
| `DONE` / `ERROR` | Completed; result waiting for the owner | IRQ handler |
| `ABORTED` | Owner died; cleanup pending | Reaper, fault path |

The reaper may free a slot only in `SUBMITTED` or `DONE`. For `IN_FLIGHT`, it must call `abort` on the device first, wait for the device to go idle, and only then release the buffer.

## 7. Device I/O on AXI

The ring is a software queue between tasks. AXI is a hardware bus with its own rules. The kernel can put a queue in front of an AXI device, but it cannot make the bus behave like one. Three rules follow.

Scope note: the first release covers byte and stream peripherals only. Section 7.1 is a deferred requirement. It must be implemented before any DMA engine is added, and it is not part of the first release.

### 7.1 DMA outlives the thread (deferred)

If a process dies while its buffer is the target of a DMA transfer, the engine keeps writing into that memory. Reaping the slot does not stop the transfer. If the kernel reuses the pages, the result is silent memory corruption.

Rule: the reaper must (1) call `abort` on the engine, (2) wait for the engine to report idle, and (3) only then free the pages.

### 7.2 A hung bus cannot be reaped

A slave that never asserts `RREADY` or `BVALID` stalls the master. There is no software state to clean up.

Rules:
- Every in-flight request has a timeout, checked from a timer tick or a watchdog.
- On timeout: abort the request, reset the device through its reset path, and mark the channel as faulted until it is reinitialized.

### 7.3 User code never sees hardware addresses

- User code submits requests into kernel-owned buffers.
- The kernel programs descriptors and registers.
- Every user-supplied address and length is validated against the process mapping before it reaches a DMA descriptor or register.

## 8. Device type decides the design

Not every AXI device needs the full machinery. Choose by device type:

| Device type | Design |
|---|---|
| Stream peripheral with a FIFO | Ring channel as in section 4. Consumer writes to FIFO. |
| Memory-mapped registers, no DMA | One channel that serializes register access. Simple, no DMA cleanup needed. |
| DMA engine with descriptors | Full design: `SUBMITTED` / `IN_FLIGHT` / `DONE` states, abort-then-free reaper, timeouts, validated buffers. |

## 9. Changes if the system becomes multi-CPU

- The head index becomes an atomic and the claim becomes compare-and-swap.
- Per-slot sequence numbers (Vyukov-style bounded queue) replace the simple state check.
- The interrupt-disable claim is replaced by the CAS. Interrupts may still be needed for ISR-safety on the same CPU.
- Per-CPU rings are worth considering to remove contention on `head`. The consumer merges by timestamp or drains each ring in turn.
- Memory barriers become required, not just compiler barriers.

## 10. Open questions

1. Which architecture (ARM, RISC-V, x86)? Affects barrier and IRQ primitives.
2. Scheduler: not yet complete. When it lands, decide whether it provides stable task IDs and a task-exit hook (required for the reaper, section 2.1).
3. Scheduler policy (preemptive or cooperative) is still undecided. A cooperative scheduler would remove the need to disable interrupts for the claim.
4. First devices in scope are byte and stream peripherals (section 8). Which specific peripherals, and do they have a FIFO?
5. Should `dropped` count copy faults, or only ring-full drops?
6. What is the maximum message size? The 120-byte limit is a placeholder.

## 11. Testing requirements

- Unit tests for the state machine on the host: claim, publish, drain, abort, reap, full ring.
- Fault-injection tests: fault during copy, task killed at each state, ISR during claim window.
- Stress test: multiple producers plus an ISR producer against a slow consumer. Verify no torn messages and that `dropped` equals messages lost.
- For device channels: timeout test with a stalled slave model, DMA abort test confirming no writes after release.

## 12. Glossary

- **Claim**: atomically taking a FREE slot for a producer.
- **Publish**: marking a slot READY after its contents are complete.
- **Drain**: consumer moving READY messages to the device.
- **Reap**: cleaning up slots left by a task that exited.
- **Abort**: marking a slot so the consumer skips it, or stopping hardware first for device channels.
