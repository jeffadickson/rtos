# TASK-NXR-221: Monitor legal transition table

Status: Planned

Requirement: NXR-221; qualification review 61/100.

## Required behavior

The target executive shall enforce the documented command-state transition table without implicit starts.

## Reason and source finding

The proposed states need executable rejection tests as well as happy-path scenarios.

## Acceptance criteria

CONFIGURE, ARM, START, STOP and REARM issued in every state either follow the table or fail unchanged.

## Verification and deliverables

Exhaustively exercise the finite state-command matrix including duplicate commands.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Fixed state enum and one active run; bounded command processing.

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
