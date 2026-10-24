# TASK-NXR-171: Optional lifecycle compatibility

Status: Planned

Requirement: NXR-171; qualification review 11/100.

## Required behavior

A future lifecycle profile shall use boot-reserved thread slots while preserving sealed registration and permanent completion in the default profile.

## Reason and source finding

Runtime lifetime support need not imply dynamic allocation but must not silently alter the current contract.

## Acceptance criteria

Default post-start create still fails; lifecycle-disabled binaries expose no runtime recycling path.

## Verification and deliverables

Compile both profiles and execute default registration regressions plus opt-in slot state fixtures.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Fixed slot and stack pool; zero general heap; lifecycle remains opt-in.

## Dependencies

NXR-001, NXR-128, NXR-129

## Inspected sources

- include/rtos.hpp
- src/kernel.cpp
- include/rtos_mutex.hpp
- include/rtos_queue.hpp

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
