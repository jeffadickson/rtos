# TASK-NXR-247: GPIO ownership and register races

Status: Planned

Requirement: NXR-247; qualification review 87/100.

## Required behavior

GPIO qualification shall prove pin ownership and correct atomic set-clear semantics under concurrent unrelated pin updates.

## Reason and source finding

Generic volatile access alone does not protect shared GPIO read-modify-write operations.

## Acceptance criteria

Two threads toggle disjoint pins without lost updates; conflicting ownership is rejected before register access.

## Verification and deliverables

Capture pins externally and exercise write-one and read-modify-write register variants.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Fixed pin table; critical section limit declared for non-atomic hardware.

## Dependencies

NXR-041, NXR-119, NXR-149

## Inspected sources

- include/rtos_uart.hpp
- docs/drivers.rst
- tests/uart.cpp

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
