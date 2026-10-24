# TASK-NXR-183: Atomic return-value oracle

Status: Planned

Requirement: NXR-183; qualification review 23/100.

## Required behavior

Zaamo qualification shall check both stored values and returned old values for every supported operation.

## Reason and source finding

Final memory checks can miss an incorrect register result from an atomic instruction.

## Acceptance criteria

Exchange, add, OR and AND return and store the expected words; signed min and max are tested only when separately advertised by the instruction model.

## Verification and deliverables

Execute edge operands and randomized word pairs; mutate return-value handling to prove detection.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Fixed operand corpus plus declared seed and operation count; no heap on target.

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
