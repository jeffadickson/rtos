Uploaded diagnostics and QSPI boot
========================================

Status and preferred design
---------------------------

This is a planned optional service, not an implemented executable loader.
Keep ELF parsing, relocation, linking and debug symbols in ground tools.
Reserve one executable RAM slot and one diagnostic worker with a permanent
stack and control block at boot. An uploaded image runs in that worker;
it does not register a new thread after the kernel seals its registry.
The minimal configuration has no overlay reservation or loader cost.

Ground relocation
-----------------

1. Read the exact running firmware build ID, target ISA/ABI, exported service
   ABI version and configured slot address/capacity from telemetry.
2. Compile a freestanding diagnostic against a small versioned service-table
   header. Link its code, constants and writable image for that exact slot.
   Resolve all relocations on the ground. Reject unresolved imports, runtime
   constructors, heap calls, TLS, exceptions and unsupported ISA instructions.
3. Keep the ELF, map, source and symbols on the ground. Generate a flat payload
   and a bounded, explicitly encoded manifest using a native C# tool with XDoc.
   Do not dump an ET_REL object and assume relocation occurred: the linker
   must resolve it first. GNU ``objcopy -O binary`` discards symbol and relocation
   information; it does not make an arbitrary object safely executable.
4. Upload bounded chunks to the inactive slot. Validate the complete package
   before publishing its entry point. Telemetry reports package ID and state.
5. The resident worker calls the entry with the service table and a fixed
   caller-owned scratch region. On return, retain result/status and release
   service ownership before accepting a replacement.

For ground debugging, report firmware ID, package ID, slot base and captured
program counter (and saved registers when available). Ground tools symbolize
against the retained exact ELF. This supplies names and source locations
without carrying a symbol table onboard; hardware breakpoint/single-step
support still requires a separate debug transport and target adapter.

The manifest identifies format version, target, firmware build ID, service ABI,
load address, initialized byte count, zero-fill offset/size, entry offset,
required alignment, stack requirement and package identity/digest. Its wire
encoding uses fixed-width fields and explicit byte order, not a native C++
structure dump. Validate every range with overflow-safe subtraction, including
entry alignment and containment in an executable initialized range. Zero-fill
must remain inside the slot and outside initialized bytes. Reject unknown
versions and any attempt to address kernel memory.

A raw binary has no BSS storage; the receiver explicitly clears the validated
zero-fill range. Link all loaded sections into a bounded slot layout: widely
separated section addresses can make a raw binary unexpectedly large.

Fixed addresses keep the target small
----------------------------------------

An image linked for one address cannot be copied to an arbitrary address.
For one fixed slot, ground linking is sufficient. For several slots, link a
package per selected slot or relocate on the ground after selecting it.
Position-independent code with a service table is another future option, but
does not by itself solve writable-data, global-pointer, TLS or ABI differences.
The initial design avoids target-side relocation records and symbol lookup.

Prefer a stable ``extern "C"`` entry and a versioned, size-tagged table of
function pointers. Do not expose C++ object layout, templates, vtables or
kernel-private structures. The table can provide bounded console output,
copied messages, metrics snapshots, deadlines and selected driver operations.
Direct imports of kernel addresses require the exact matching firmware;
even with a table, the first implementation requires an exact build ID.
An uploaded shell is a diagnostic client of these services, not a process.
For RISCue, retain the firmware's ``-msmall-data-limit=0`` and ``-mno-relax``
policy and preserve the calling convention's gp/tp registers. Do not assume a
separately linked module can install its own global pointer before calling
resident services. Other ports need their own checked compiler/ABI contract.

Activation and lifetime
-----------------------

Use explicit empty, receiving, validated, running and fault states. Only one
owner may modify the slot. Reject upload/replacement while the worker is
executing or any callback, DMA operation or other thread still references it.
No overlay pointer may escape into a persistent driver, queue or interrupt
registration. All queued results must be copied into resident storage.
The initial interface permits no retained callbacks or peripheral ISR handlers.

Before execution, a board hook must complete data writes, clean data cache
where needed, invalidate instruction cache and perform the architecture's
instruction synchronization. The current RV32IM/Zicsr profile does not declare
Zifencei; do not insert ``fence.i`` without establishing core support and the
actual instruction-fetch coherency contract. Unsupported executable-memory
publication must reject activation. GCC's cache-clear builtin can lower to a
runtime helper; it is not proof of a complete board cache protocol.

Use command authorization and image authentication according to mission policy.
A checksum/digest detects corruption but is not authentication. Failed or
partial transfers never become executable. A single slot minimizes RAM but
cannot retain a previous RAM image for rollback; keep a known-good resident
monitor and optionally retain packages in nonvolatile storage.

The current machine-mode kernel has no memory isolation. Uploaded native code
is as privileged as the kernel. A software deadline cannot contain code that
disables interrupts, stalls AXI or never yields. Recovery from a hung diagnostic
requires a qualified watchdog/reset path; a supervisor thread alone is not
containment. Do not forcibly reuse mutexes or device state after killing code.

Resource budget
---------------

Budget the slot's code/constants/data/BSS, resident worker stack/control block,
fixed receiver state and bounded transfer buffers explicitly. The worker stack
includes application call depth, the 144-byte RISCue trap frame and kernel
calls. Declare and check the module's required stack against the permanent
worker stack. Avoid another full-size staging buffer: receive into the idle
slot and validate it in place. This permits runtime code replacement while
all storage capacity remains fixed at boot; it does not eliminate RAM cost.

QSPI: boot storage versus execution
-----------------------------------

QSPI is useful for storing the boot image and optional diagnostic packages.
There are two distinct board arrangements:

* **Copy boot:** a small reset loader configures QSPI, copies the image into
  RAM, initializes data/BSS and starts it. Flash holds the image persistently,
  but executing code still occupies RAM.
* **Execute in place (XIP):** a memory-mapped controller serves CPU instruction
  fetches directly from flash. Code and constants can remain there; writable
  data, stacks, driver buffers and diagnostic RAM slots still require RAM.

An SPI/QSPI peripheral alone does not establish XIP. Verify the FPGA reset
path, bus mapping, instruction-fetch support, flash mode setup, cache and wait
states, worst-case interrupt latency, and behavior during erase/program.
Keep vectors, critical handlers and flash-program routines in RAM when the
board's flash-busy behavior requires it. The linker must distinguish flash
load addresses from RAM execution/data addresses, and startup must copy data.
The current RISCue flat-RAM linker/startup does neither and remains unchanged
in this respect. No generic flash address is assumed.

For the current nano image, 3812 bytes are code/constants in RAM. Qualified
XIP could move those bytes out of RAM, but the controller, linker layout,
startup and any RAM-resident routines change the final measurement.
No XIP RAM saving is claimed as a measured result today.

References
----------

* `GNU objcopy binary output <https://www.sourceware.org/binutils/docs/binutils/objcopy.html>`_
* `GCC cache-clear builtin <https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html>`_
