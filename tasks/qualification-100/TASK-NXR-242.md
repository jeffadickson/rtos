# TASK-NXR-242: UART acceptance accounting

Status: Planned

Requirement: NXR-242; qualification review 82/100.

## Required behavior

UART counters shall distinguish accepted, rejected-full, truncated, transmitted and failed records.

## Reason and source finding

A hello demo cannot establish that overload and truncation are visible to callers.

## Acceptance criteria

Saturate the queue and formatting limit; every producer attempt has one documented outcome and byte conservation reconciles.

## Verification and deliverables

Use known record lengths and controlled transport errors with counter boundary fixtures.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Saturating diagnostic counters; fixed queue capacity.

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
