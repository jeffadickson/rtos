# TASK-NXR-172: Thread generation exhaustion

Status: Planned

Requirement: NXR-172; qualification review 12/100.

## Required behavior

A recyclable thread handle shall contain a generation that never wraps into a previously valid handle during a boot.

## Reason and source finding

Index-only handles would allow stale commands to control a different thread incarnation.

## Acceptance criteria

An old handle fails after slot reuse; the final representable generation exhausts the slot rather than wrapping.

## Verification and deliverables

Use a reduced-width generation fixture and validate real-width boundary arithmetic.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

One generation word per reserved slot; exhausted slots remain unavailable until reset.

## Dependencies

NXR-001, NXR-128, NXR-129

## Inspected sources

- include/rtos.hpp
- src/kernel.cpp
- include/rtos_mutex.hpp
- include/rtos_queue.hpp

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
