# TASK-NXR-197: Failure minimization preservation

Status: Planned

Requirement: NXR-197; qualification review 37/100.

## Required behavior

A failure reducer shall preserve the original bundle and emit every reduced candidate as a separately identified artifact.

## Reason and source finding

Simplifying a randomized failure can accidentally remove the trigger while retaining a similar symptom.

## Acceptance criteria

A reduced workload that stops reproducing is rejected as a reproducer; original inputs remain immutable.

## Verification and deliverables

Replay a seeded injected failure, remove operations and verify stable failure identity.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Finite reduction attempts and host time budget; no autonomous unbounded search.

## Dependencies

NXR-072, NXR-073, NXR-094

## Inspected sources

- include/rtos_trace.hpp
- include/rtos_fault_record.hpp
- docs/hardware-in-loop.rst

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
