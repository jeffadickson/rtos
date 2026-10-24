# TASK-NXR-174: Thread join evidence

Status: Planned

Requirement: NXR-174; qualification review 14/100.

## Required behavior

An optional join operation shall distinguish running, completed, timed-out and stale generations without consuming another incarnation's completion.

## Reason and source finding

The current finished state is not a join or storage-reclamation protocol.

## Acceptance criteria

Two joiners observe the documented policy; timeout and completion races never return an unrelated generation.

## Verification and deliverables

Enumerate completion-before-wait, wait-before-completion and timeout boundaries in a state model.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Fixed join waiter capacity; no allocation and no infinite cleanup scan.

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
