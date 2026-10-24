# TASK-NXR-244: UART receiver overrun policy

Status: Planned

Requirement: NXR-244; qualification review 84/100.

## Required behavior

A future UART receiver shall expose bounded storage, per-error counters and a documented record resynchronization rule.

## Reason and source finding

The supplied AXI adapter is TX-only and must not be described as a complete RX or interrupt driver.

## Acceptance criteria

Overflow during a stalled reader does not corrupt adjacent memory and subsequent delimiter-framed packets recover.

## Verification and deliverables

Inject framing errors, bursts beyond capacity and consumer pauses on real hardware.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Fixed RX ring and finite ISR work per entry; no heap.

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
