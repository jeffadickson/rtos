# TASK-NXR-181: Independent RISC-V feature matrix

Status: Planned

Requirement: NXR-181; qualification review 21/100.

## Required behavior

Qualification shall build baseline, Zaamo-only, M/U-PMP-only and combined profiles as four independent configurations.

## Reason and source finding

Atomics and privilege protection are separate axes and should not become one coupled feature.

## Acceptance criteria

Each profile reports its actual ISA and privilege capability; disabling either extension preserves baseline behavior.

## Verification and deliverables

Compile and execute profile-specific positive and illegal-operation fixtures.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Single hart; fixed four-region PMP profile where enabled; no implicit MMU.

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
