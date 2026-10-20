Start with three patterns
================================================================================

The kernel stays small by enabling only the services the application uses.
For most applications, start with threads, copied messages, and queued output.
Keep DMA, PCIe, networking and board bring-up separate from the first application.
The starter uses project-owned startup and bounded output; :ref:`libgloss-overview`
explains where newlib/libgloss would fit if C-library compatibility is added.

Use one include
--------------------------------------------------------------------------------

Enable RTOS_SYNC, and RTOS_LOGGING when using queued printf. Applications may
include rtos_ipc.hpp; the individual headers remain available. Objects and stacks
must have permanent boot-provided storage. These services currently support one
hart. The detail header is kernel implementation, not an application API.

Pass a value
--------------------------------------------------------------------------------

.. code-block:: cpp

   #include "rtos_ipc.hpp"
   struct Sample { uint32_t channel; int32_t value; };
   static rtos::Queue<Sample, 4> samples;

   // Producer thread:
   auto status = samples.send({1, 42}); // NoWait: check full explicitly.
   // Consumer thread:
   Sample sample{};
   status = samples.receive(sample, 100); // At most 100 ticks waiting.
   if (status == rtos::Result::ok) {
       // Process sample here.
   }

Messages are copied. A failed receive leaves sample unchanged. Keep message
types small and trivial; sending a pointer does not transfer its pointee's
ownership. Timeout values are ticks, not milliseconds. Blocking calls need a
running application thread; construct objects at boot and use them from threads.

Protect a device transaction
--------------------------------------------------------------------------------

.. code-block:: cpp

   static rtos::DeviceTransactions device;
   auto status = device.transact(100, [&]() {
       // Perform the complete command/data/status transaction.
       // Register access and hardware completion belong to the board driver.
       return rtos::Result::ok;
   });

Threads wait for ownership rather than spin. The mutex uses priority inheritance
and releases normally when the callback returns, including an error return.
The callback may wait on a verified completion primitive, but must have a bounded
operation policy. A hardware-stalled AXI access still needs a hardware timeout.

Print a diagnostic
--------------------------------------------------------------------------------

.. code-block:: cpp

   static rtos::Logger<4, 64> console;
   auto result = console.printf("channel=%u value=%d\n", 1u, 42);
   // A permanent output thread calls this with a synchronous UART callback:
   auto output = console.drain(uart_write_some, uart_context, 32);

The producer formats privately and never waits for UART output. Full queues
return full and drop the new record. Check result.truncated when diagnostics
must fit. This is a bounded printf subset, not a replacement for libc printf.
The writer consumes/copies bytes before returning; partial writes and temporary
backpressure retain the pending record. It must not retain pointers or start DMA.
One logger must own the shared console path to preserve record order.

The complete examples/console.cpp uses a counting sink rather than a UART, so
it can cross-link without a guessed register map. Replace that sink and the
example timer frequency with real, verified board hooks before running it.

.. code-block:: sh

   cmake --preset logging -B build/console -DRTOS_APPLICATION=examples/console.cpp \
     -DRISCV_TOOLCHAIN_PREFIX=/path/to/riscv64-unknown-elf-
   cmake --build build/console

Reach for advanced services when needed
--------------------------------------------------------------------------------

* EventFlags::wait_any / wait_all: readiness bits; EventClear::yes consumes bits.
* Semaphore: counted work or available resources; it has no mutex ownership.
* AtomicU32: small shared RAM words serialized on the current hart.
* Snapshots: inspect priorities, queue occupancy, drops and device ownership.

The synchronization chapter has the full error, lifetime, ISR and recovery
contracts. Peripheral interrupt dispatch and physical UART qualification remain
board work. No host check establishes FPGA timing or multicore correctness.
