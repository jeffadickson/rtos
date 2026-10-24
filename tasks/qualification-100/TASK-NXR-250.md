# TASK-NXR-250: I2C stuck-bus recovery evidence

Status: Planned

Requirement: NXR-250; qualification review 90/100.

## Required behavior

I2C recovery shall use a board-declared bounded procedure and report failure when SCL or SDA cannot be released.

## Reason and source finding

Recovery must be based on actual line state and board electrical constraints.

## Acceptance criteria

NACK, clock stretching and permanently low lines yield distinct terminal outcomes without indefinite polling.

## Verification and deliverables

Use programmable pull-down fixtures and capture recovery pulses and stop generation.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Finite stretch and recovery pulse limits; no unconditional bus-reset loops.

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
