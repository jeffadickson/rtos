# TASK-NXR-188: Trap origin integrity

Status: Planned

Requirement: NXR-188; qualification review 28/100.

## Required behavior

Trap dispatch shall preserve and report the originating privilege mode without allowing user-controlled state to impersonate kernel origin.

## Reason and source finding

Shared trap entry must distinguish a legitimate user request from a kernel exception.

## Acceptance criteria

User ecall and illegal CSR access are classified correctly; nested fault fixtures retain original context evidence.

## Verification and deliverables

Compare saved status and cause against independently calculated trap transitions.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Fixed trap frames; bounded nesting policy explicitly declared.

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
