# TASK-NXR-245: External interrupt acknowledgement

Status: Planned

Requirement: NXR-245; qualification review 85/100.

## Required behavior

Peripheral IRQ qualification shall establish source identification, acknowledgement order and pending recheck without losing a concurrent event.

## Reason and source finding

Current timer-only dispatch cannot establish a working UART or DMA external interrupt path.

## Acceptance criteria

An event arriving during acknowledgement is eventually serviced once; spurious sources do not call unrelated handlers.

## Verification and deliverables

Use hardware-generated edge and level stimuli and an independent event count.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Fixed handler table; bounded per-entry service and declared nesting policy.

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
