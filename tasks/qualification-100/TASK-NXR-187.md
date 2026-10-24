# TASK-NXR-187: PMP lock semantics

Status: Planned

Requirement: NXR-187; qualification review 27/100.

## Required behavior

PMP lock qualification shall verify the documented reset-only unlock behavior and machine-mode access effects.

## Reason and source finding

A simulated region table can miss architectural lock effects and their reset lifetime.

## Acceptance criteria

Writes to locked configuration fail to change it; accesses obey the selected locked permissions until reset.

## Verification and deliverables

Attempt configuration and address rewrites and compare reset and pre-reset behavior.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

No runtime unlock API; fixed board reset domain.

## Dependencies

NXR-102, NXR-139

## Inspected sources

- include/rtos_hal.hpp
- include/rtos_hal_atomic.hpp
- sim/interpreter/machine.hpp
- sim/interpreter/machine.cpp
- ports/riscue/context.S

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
