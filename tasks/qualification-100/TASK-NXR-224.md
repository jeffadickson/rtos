# TASK-NXR-224: Rearm control-queue preservation

Status: Planned

Requirement: NXR-224; qualification review 64/100.

## Required behavior

REARM shall reset only settled test resources and preserve permanent monitor control channels.

## Reason and source finding

Permanent control waiters are legitimate idle state and must not be drained as leaked test resources.

## Acceptance criteria

An idle control waiter survives REARM and accepts the next command; stale job descriptors are rejected.

## Verification and deliverables

Repeat configure-run-stop-rearm cycles with queued old-generation work and idle control waiters.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

No blanket queue reset; fixed run-generation metadata.

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
