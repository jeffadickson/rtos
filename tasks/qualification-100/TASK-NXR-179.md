# TASK-NXR-179: Worker activation conservation

Status: Planned

Requirement: NXR-179; qualification review 19/100.

## Required behavior

Permanent-worker test jobs shall use unique run and job identities and reject activation while the prior job is unsettled.

## Reason and source finding

Boot-created workers already support repeated jobs without promising runtime thread creation.

## Acceptance criteria

Duplicate activation executes once; a new run cannot consume a queued old-run command.

## Verification and deliverables

Use a bounded command queue with replayed commands and delayed acknowledgements.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

One active job per worker and a fixed outstanding command window.

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
