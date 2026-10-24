NexRT qualification roadmap
===========================

The 100 reviews in this campaign refine the proposed NexRT distinction into
measurable obligations: predictable resources, understandable kernel contracts,
independent RISC-V configurations, reproducible failures, hardware-backed tests,
and drivers with explicit ownership and recovery. They are requirements design
iterations, not 100 implemented runtime changes or hardware qualification runs.

The canonical catalogue adds NXR-161 through NXR-260. Each has a normative
statement, specific acceptance, verification procedure, dependencies, resource
ceiling and source finding. All are Planned. Existing requirements retain their
prior status; these refinements do not silently replace their contracts.

Git-tracked tasks
-----------------

Each imported requirement generates one Markdown task under
``tasks/qualification-100``. Its stable identity is ``TASK-NXR-nnn``. Tasks contain
the full required behavior, acceptance criteria, verification deliverables,
configuration, resource ceiling, dependencies, inspected sources and completion
checklist. These are local repository tasks; no remote issues are created.
The C# tracker checks their exact content and rejects stale or missing output.
Unchecked tasks represent implementation and evidence still to deliver, even
when their requirements review has completed.

.. list-table:: Review batches
   :header-rows: 1

   * - Reviews / requirements
     - Deliverable focus
   * - 1-10 / NXR-161..170
     - RAM, allocation detection, interrupt masking, stack and performance evidence
   * - 11-20 / NXR-171..180
     - Optional lifecycle generations, join, stop and safe reclamation
   * - 21-30 / NXR-181..190
     - Independent Zaamo and M/U-PMP profiles; interpreter timing and RTL comparison
   * - 31-40 / NXR-191..200
     - Versioned random streams, replay, first witnesses and detector qualification
   * - 41-50 / NXR-201..210
     - Fixed 512-byte protocol, CRC, COBS, sessions and idempotent retry
   * - 51-60 / NXR-211..220
     - C# multi-board host leases, supervision, durable evidence and recovery
   * - 61-70 / NXR-221..230
     - Target executive transitions, quiescence, bounded storage and rearm
   * - 71-80 / NXR-231..240
     - Independent FPGA/UART observer, calibration, timing and reset evidence
   * - 81-90 / NXR-241..250
     - UART, external IRQ, GPIO, watchdog, SPI and I2C qualification
   * - 91-100 / NXR-251..260
     - DMA bus ownership, evidence-based release claims and milestone integrity

Relationship to the HIL proposal
--------------------------------

The 60 original HIL design IDs remain available in :doc:`hil-verification-plan`
and its CSV. They are not silently renumbered or deleted. The canonical
requirements refine the same architecture: HIL protocol obligations map to
NXR-201..210, host obligations to NXR-211..220, monitor obligations to
NXR-221..230, equipment obligations to NXR-231..240, and test acceptance is
distributed across resource, kernel, replay and driver batches. This is an
area-level relationship, not a claim that every prior HIL item has passed.

Compatibility and claim limits
------------------------------

Default registration remains boot-only and completion permanent. Future slot
recycling is opt-in and uses boot-reserved storage. Zaamo, privilege/PMP and
instruction-model timer batching are independent switches. Hardware qualification
requires hardware evidence: host tests and source review cannot establish bus
quiescence, external IRQ delivery, physical UART integrity or PMP hardware timing.

The current AXI UART adapter is TX-only and polling; it does not initialize the
board clock or supply an RX/external-interrupt implementation. The DMA backend
must prove quiescence. The existing first-fault helper is not reset-retained.
These limitations are explicit tasks rather than supported-feature claims.

Verification and milestones
--------------------------------

The C# command ``qualification-checkpoint ROOT N`` imports the next ten reviews
for boundaries 10,20,...,100. It builds the host code and optimized overnight
firmware, runs host CTest and static allocation contracts, and builds Sphinx
documentation before committing that boundary locally. It does not launch a soak,
program a board, or push. Five-minute subprocess deadlines fail the checkpoint.
Failed commands prevent a milestone commit. Logs, input hashes and parent commit
identity are retained in ``requirements/qualification-100/checkpoints``.

The authored source is ``requirements/qualification-100/reviews.psv``; catalogue,
StrictDoc, traceability and task exports are deterministic C# outputs. The
project uses a StrictDoc-format export, not the external StrictDoc engine.
Documentation source changes to inspected files require regenerating fingerprints.
