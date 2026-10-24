# TASK-NXR-185: User entry register sanitation

Status: Planned

Requirement: NXR-185; qualification review 25/100.

## Required behavior

An optional U-mode entry shall initialize privilege state and all exposed register contents according to a documented ABI.

## Reason and source finding

Processor U-mode support does not by itself supply a safe operating-system user boundary.

## Acceptance criteria

Entry cannot inherit privileged stack pointers or stale kernel data; legal user entry reaches its declared PC.

## Verification and deliverables

Use register-pattern fixtures and inspect mstatus, mepc and return privilege on an independent execution model.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

One declared user frame per slot; no FP or vector state unless separately enabled.

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
