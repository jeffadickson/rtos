# TASK-NXR-178: Lifecycle priority cleanup

Status: Planned

Requirement: NXR-178; qualification review 18/100.

## Required behavior

Slot reuse shall discard inherited priority and donation links from the retired generation before publishing the new thread.

## Reason and source finding

Donation metadata belongs to an incarnation rather than a reusable storage address.

## Acceptance criteria

A reused low-priority thread cannot inherit a prior owner's donated priority or remain in its chain.

## Verification and deliverables

Build chained donation fixtures and reuse each retired slot position.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Cleanup bounded by configured mutex and thread counts.

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
