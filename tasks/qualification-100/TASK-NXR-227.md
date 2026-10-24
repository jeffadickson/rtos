# TASK-NXR-227: Command descriptor ownership

Status: Planned

Requirement: NXR-227; qualification review 67/100.

## Required behavior

Monitor queues shall carry bounded frame descriptors rather than copying full 512-byte packets under the kernel queue lock.

## Reason and source finding

The fixed packet size exceeds the existing queue copy limit and needs separate frame storage.

## Acceptance criteria

Largest command enqueue respects the 128-byte copy limit; descriptor ownership is conserved on reject and cancel.

## Verification and deliverables

Fill every pool slot and inject parse failures, duplicate responses and cancellation.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Static frame pool and descriptor queue; proposed protocol storage ceiling 6784 bytes excluding stacks and descriptors.

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
