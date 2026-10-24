# TASK-NXR-182: Zaamo capability mismatch

Status: Planned

Requirement: NXR-182; qualification review 22/100.

## Required behavior

The platform shall reject an atomic-capable build when the selected execution target or RAM window lacks the required Zaamo contract.

## Reason and source finding

Instruction availability alone does not establish atomic behavior for every mapped address.

## Acceptance criteria

Wrong-capability manifests fail before workload release; baseline fallback uses its explicitly selected implementation.

## Verification and deliverables

Run manifest mismatch controls and RAM versus MMIO atomic address fixtures.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

No atomics on device memory; no silent runtime emulation inside the hardware qualification profile.

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
