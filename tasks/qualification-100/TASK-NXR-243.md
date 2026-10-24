# TASK-NXR-243: UART boot and BSP contract

Status: Planned

Requirement: NXR-243; qualification review 83/100.

## Required behavior

UART initialization shall declare device base, clock, reset, baud and ordered-MMIO guarantees before the service worker runs.

## Reason and source finding

The current AXI TX adapter intentionally does not initialize clocks, baud or reset.

## Acceptance criteria

Misaligned base and wrong device profile are rejected; measured pin baud matches the configured tolerance.

## Verification and deliverables

Combine address-validation host fixtures with board clock and pin-loopback measurements.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

No runtime driver discovery; one permanent owner per configured UART.

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
