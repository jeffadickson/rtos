# TASK-NXR-256: No silent hardware skips

Status: Planned

Requirement: NXR-256; qualification review 96/100.

## Required behavior

Qualification shall distinguish passed, failed, unsupported, not-run and incomplete for every required board-profile test.

## Reason and source finding

A green aggregate is misleading if unsupported hardware tests disappear from the denominator.

## Acceptance criteria

Remove required UART checker or unavailable PMP support; aggregate release status cannot remain passed.

## Verification and deliverables

Validate result aggregation with mixed result fixtures and missing required evidence.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Finite declared test matrix; no implicit default-pass entries.

## Dependencies

NXR-134, NXR-135, NXR-159

## Inspected sources

- include/rtos_dma.hpp
- src/dma.cpp
- tests/dma.cpp
- docs/dma.rst

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
