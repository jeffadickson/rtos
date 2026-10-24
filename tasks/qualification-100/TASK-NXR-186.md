# TASK-NXR-186: PMP boundary access matrix

Status: Planned

Requirement: NXR-186; qualification review 26/100.

## Required behavior

Four-region PMP qualification shall test the last allowed and first forbidden byte for every configured access permission.

## Reason and source finding

Region setup needs boundary evidence rather than only checking CSR readback.

## Acceptance criteria

Read, write and execute violations trap with the expected cause and address; permitted boundary accesses succeed.

## Verification and deliverables

Generate byte, halfword and word probes including region-crossing accesses on RTL and interpreter.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Exactly four declared regions; reject unrepresentable layouts before enabling user execution.

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
