# TASK-NXR-176: Abandoned mutex policy

Status: Planned

Requirement: NXR-176; qualification review 16/100.

## Required behavior

The lifecycle API shall reject normal reclamation of a thread that still owns a mutex and report the ownership violation.

## Reason and source finding

Releasing a dead owner's lock can expose partially updated application state.

## Acceptance criteria

Exit or stop with one and multiple owned mutexes produces the declared fault without silently transferring protected data.

## Verification and deliverables

Exercise nested ownership and inherited priority chains with an independent ownership ledger.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

No forced unlock; reporting bounded by configured owned-mutex metadata.

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
