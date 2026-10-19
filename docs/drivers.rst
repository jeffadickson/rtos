Drivers and Ethernet integration
================================

Driver architecture
-------------------

Keep board register addresses and DMA details in a board support package.
Reserve driver objects, descriptors, buffers, and worker threads statically.
Initialize hardware at boot, then hand each peripheral to one owning thread.
Every operation needs a timeout or bounded work budget, a documented ownership
rule, and a defined response to unavailable hardware or exhausted storage.

The current kernel enables only timer interrupts. Polling peripheral integration
is possible immediately; interrupt-driven drivers need a verified external
interrupt dispatch/acknowledgement extension. Do not enable external interrupts
and assume a FreeRTOS ISR API exists.

.. list-table:: Board driver inventory
   :header-rows: 1
   :widths: 20 20 30 30

   * - Driver
     - Priority/status
     - Static resources
     - Integration/test requirement
   * - Machine timer
     - Implemented in RISCue port
     - Timer state; no queue
     - Clock calibration, latency, compare-race testing on RTL
   * - Trap/interrupt controller
     - Timer only; external dispatch needed
     - Handler table and optional deferred-event slots
     - Source acknowledge, masking, nesting policy, storm tests
   * - UART console
     - First board addition
     - Fixed RX/TX byte rings
     - Overflow counters; nonblocking diagnostics; baud/loopback tests
   * - GPIO/pin control
     - Early board addition
     - Pin ownership/configuration table
     - Atomic register semantics, debounce policy, board mapping
   * - Reset/watchdog
     - Recommended for deployed systems
     - Supervisor state and retained reset record
     - Progress-based feeding; hung-thread and reset-reason tests
   * - Ethernet MAC
     - Adapter implemented; MAC not supplied
     - Aligned fixed RX/TX descriptor rings and packet buffers
     - Existing MAC glue, DMA/cache ordering, saturation and exhaustion tests
   * - Ethernet PHY/MDIO
     - Needed with an external PHY
     - PHY/link state and bounded transaction state
     - Link negotiation, cable removal, timeout/recovery tests
   * - SPI
     - As board requires
     - Transaction slots and DMA buffers
     - Chip-select ownership, timeout, bus arbitration
   * - I2C
     - As board requires
     - Fixed transaction queue
     - NACK, stuck bus, recovery and deadline handling
   * - DMA engine/cache support
     - Hardware dependent
     - Descriptor pool with documented alignment
     - Ownership barriers and coherency under concurrent transfers
   * - Flash/QSPI/EEPROM
     - For configuration or firmware update
     - Page buffers and operation state
     - Erase timing, power-fail recovery, wear policy
   * - RTC/calendar
     - If wall time is needed
     - Clock synchronization state
     - Clock changes must not change scheduling mtime
   * - SD/block storage
     - Optional
     - Sector buffers and static request slots
     - Bounded filesystem integration, removal and fault tests
   * - CRC/RNG/crypto
     - Optional hardware acceleration
     - Explicit session/key storage
     - Known-answer tests and sensitive-data lifecycle
   * - ADC/PWM/CAN
     - Application dependent
     - Sample/frame rings and channel tables
     - Sampling deadlines, bus errors, overruns, safe output state

Ethernet adapter contract
-------------------------

``include/rtos_ethernet.hpp`` defines a small optional adapter and MAC callback
interface. It deliberately does not choose a board MAC, vendor BSP, PHY, or TCP/IP
implementation. Existing working code can be wrapped behind three callbacks:

* ``receive`` copies one complete frame into supplied capacity and reports length.
  It returns empty/busy when no frame is available, error on a driver failure.
  It must never write beyond capacity. Recycle the hardware descriptor after copy.
* ``transmit`` consumes or copies the caller's bytes before returning. Return
  busy for bounded backpressure; never retain that pointer for asynchronous DMA.
* ``link_up`` returns current link status without waiting for negotiation.

The protocol input callback may inspect/copy an RX frame during its invocation
only. It returns false if rejected. No callback may recursively call ``poll``
or invoke the adapter concurrently. Use one network worker to own all adapter
and protocol calls, including statistics. Reentrancy is a contract, not a lock.

``poll(budget)`` attempts at most budget receives, stopping at empty, busy, or
error. Callback execution time must also be bounded; a budget alone cannot
bound a callback that waits forever. Frames shorter than the 14-byte Ethernet
header or larger than 1536 bytes are dropped. The MAC is responsible for TX
padding, frame check sequence generation/stripping, DMA alignment, cache
maintenance, and hardware memory barriers. Jumbo frames are unsupported.

``initialize`` succeeds once, when all callbacks are present; call during boot.
It returns false on a second call or missing callback without changing state.
Before initialization, link is false, poll returns zero, and send returns error.
``send`` also returns error for null data or invalid length. Counters record
received frames, accepted transmissions, input/length drops, driver errors, and
TX busy results. Validation failures on send are returned without incrementing
driver-error count. RX busy/empty are not packet drops. Hardware receive-drop
counters must be exposed separately by the MAC driver.

Example glue pattern
--------------------

.. code-block:: cpp

   static rtos::net::Ethernet ethernet;
   // Wrap your existing MAC receive/transmit/link functions to match Mac.
   // Call ethernet.initialize(mac, protocol_input, protocol_context) at boot.
   void network_worker(void*) {
       for (;;) {
           ethernet.poll(4);
           // Service protocol timers in the same thread, with bounded work.
           rtos::sleep_for(1);
       }
   }

The sleep controls CPU load and adds receive latency. Size static rings for the
longest polling interval plus higher-priority interference and burst traffic.
Measure loss/throughput under load. This pattern is a starting integration,
not a line-rate Ethernet claim.

Reusing lwIP or another stack
-----------------------------

A practical initial lwIP integration is NO_SYS=1 with all raw-API input,
protocol timers, and output calls confined to the network worker. Upstream
documents that this mode has no OS semaphores, mutexes, or mailboxes, and requires
periodic ``sys_check_timeouts`` calls. With Ethernet, supply the upstream input
path appropriate for Ethernet framing. Follow the upstream version's
`initialization instructions <https://www.nongnu.org/lwip/2_1_x/sys_init.html>`_
and `NO_SYS constraints <https://www.nongnu.org/lwip/2_1_x/group__lwip__opts__nosys.html>`_.

This project does not yet provide that glue, lwIP sources, sockets, DHCP, DNS,
or TCP/IP tests. NO_SYS=0 integration needs a complete sys_arch port and verified
static synchronization/mailbox primitives, which are not implemented here.

For any stack, reserve protocol storage and frame pools at boot. Bounded loans
from fixed pools require explicit return paths and exhaustion tests. A static
backing heap alone does not prove leak freedom. Audit stack-internal allocation,
fragmentation, and lost-buffer behavior separately; the kernel's allocator-symbol
check does not certify a third-party stack. Existing vendor stack code may be
reused after these storage and single-thread ownership contracts are established.

Planned shared-device and IPC contracts
---------------------------------------

.. toctree::
   :maxdepth: 1

   device-ipc-review

DMA driver support remains deferred to its optional BSP roadmap. Static IPC,
transaction mutexes and queued logging now have source implementations; see
synchronization for APIs and verification limits. A general driver registry,
external interrupt dispatch and board MMIO drivers remain integration work.
