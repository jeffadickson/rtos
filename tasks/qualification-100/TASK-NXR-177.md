# TASK-NXR-177: Cancellation wait removal

Status: Planned

Requirement: NXR-177; qualification review 17/100.

## Required behavior

An optional cancellation path shall remove exactly the cancelled generation's pending wait under the same serialization as grant and timeout.

## Reason and source finding

A second terminal path can duplicate grants unless it shares the existing wait arbitration.

## Acceptance criteria

Grant, timeout and cancel permutations yield one terminal result and conserve queue items and semaphore tokens.

## Verification and deliverables

Exhaustively model finite interleavings and run seeded target races.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

At most configured waiter scans; no callback or device access inside the wait lock.

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
