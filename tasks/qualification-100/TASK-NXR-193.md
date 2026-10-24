# TASK-NXR-193: Job oracle independence

Status: Planned

Requirement: NXR-193; qualification review 33/100.

## Required behavior

Each randomized job family shall have a reference calculation that does not reuse the tested implementation path.

## Reason and source finding

A checksum of the implementation's own answer is not an independent correctness oracle.

## Acceptance criteria

A deliberate error in arithmetic, queue ordering or payload generation yields a mismatch even with a valid transport CRC.

## Verification and deliverables

Use separately authored reference functions and mutation fixtures for each job family.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Fixed input and result sizes; bounded outstanding jobs.

## Dependencies

NXR-072, NXR-073, NXR-094

## Inspected sources

- include/rtos_trace.hpp
- include/rtos_fault_record.hpp
- docs/hardware-in-loop.rst

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
