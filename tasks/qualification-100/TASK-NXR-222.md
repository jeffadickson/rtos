# TASK-NXR-222: All-worker start barrier

Status: Planned

Requirement: NXR-222; qualification review 62/100.

## Required behavior

RUNNING shall be reported only after every selected worker acknowledges the same run generation.

## Reason and source finding

Dispatching START does not prove that all required workers became active.

## Acceptance criteria

Delay or omit one worker acknowledgement; the monitor remains starting or fails its deadline without claiming full activity.

## Verification and deliverables

Gate workers individually and compare acknowledgements with externally observed activity.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

One acknowledgement bit per configured worker and finite start deadline.

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
