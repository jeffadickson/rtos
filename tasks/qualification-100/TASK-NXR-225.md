# TASK-NXR-225: Fault state quiescence distinction

Status: Planned

Requirement: NXR-225; qualification review 65/100.

## Required behavior

FAULTED shall report quiescence independently and forbid reuse when ownership remains uncertain.

## Reason and source finding

A fault label is not proof that external activity has stopped.

## Acceptance criteria

Inject a worker fault during active DMA; fault evidence is retained while reuse remains blocked.

## Verification and deliverables

Model fault and stop transitions and inspect hardware ownership until reset.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Separate fault and quiescence fields; one retained first-fault slot.

## Dependencies

NXR-039, NXR-048

## Inspected sources

- src/kernel.cpp
- docs/hil-verification-plan.rst
- include/rtos_queue.hpp

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
