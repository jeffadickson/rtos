# TASK-NXR-173: Cooperative stop acknowledgement

Status: Planned

Requirement: NXR-173; qualification review 13/100.

## Required behavior

A stop request shall be acknowledged as complete only after the worker reaches a documented safe point and settles owned resources.

## Reason and source finding

Setting a stop flag is not evidence that a worker is safe to reuse.

## Acceptance criteria

A worker retaining a mutex or DMA ownership cannot report stopped; a safe worker acknowledges once.

## Verification and deliverables

Pause workers at each ownership transition and issue duplicate stop requests.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

One stop flag and acknowledgement per permanent worker; finite grace deadline.

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
