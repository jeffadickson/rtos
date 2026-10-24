# TASK-NXR-228: Terminal evidence acknowledgement

Status: Planned

Requirement: NXR-228; qualification review 68/100.

## Required behavior

The monitor shall retain final result and fault records until the host acknowledges the matching logical identity.

## Reason and source finding

A transport write success does not establish durable host receipt.

## Acceptance criteria

Drop result frames and reconnect; identical logical evidence remains fetchable and is not replaced by a new run.

## Verification and deliverables

Exercise lost acknowledgement, duplicate fetch and stale acknowledgement cases.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

One retained terminal result and first fault; finite fetch payloads.

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
