# TASK-NXR-249: SPI transaction cancellation

Status: Planned

Requirement: NXR-249; qualification review 89/100.

## Required behavior

SPI cancellation shall retain chip select and buffer ownership until the controller is demonstrably quiescent.

## Reason and source finding

A mutex around submission alone does not serialize the full hardware transaction lifetime.

## Acceptance criteria

Cancel mid-transfer and on completion boundary; no new client begins before settled bus ownership.

## Verification and deliverables

Observe chip select and clock while injecting completion delay and controller faults.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Fixed transaction slots and finite cancellation deadline; uncertain hardware quarantined.

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
