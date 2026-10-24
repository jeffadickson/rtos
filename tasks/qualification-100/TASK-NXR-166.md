# TASK-NXR-166: Timing units provenance

Status: Planned

Requirement: NXR-166; qualification review 6/100.

## Required behavior

Every timing result shall carry target frequency, counter width, conversion rule and measurement origin.

## Reason and source finding

Host elapsed time and simulated timer advancement were previously easy to confuse.

## Acceptance criteria

Changing frequency without regenerating the profile rejects qualification; host seconds cannot populate target-time fields.

## Verification and deliverables

Exercise two clock profiles and counter wrap with known interval fixtures.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Conversions use checked arithmetic and a declared rounding bound of at most one target tick.

## Dependencies

NXR-081, NXR-025

## Inspected sources

- include/rtos.hpp
- src/kernel.cpp
- src/scheduler.hpp
- docs/memory.rst

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
