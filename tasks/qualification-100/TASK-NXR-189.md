# TASK-NXR-189: Interpreter timer service modes

Status: Planned

Requirement: NXR-189; qualification review 29/100.

## Required behavior

The interpreter shall separate instruction-exact timer delivery from opt-in batched delivery and report the configured delay bound.

## Reason and source finding

Reducing timer checks can improve throughput while changing interrupt placement and must be visible.

## Acceptance criteria

Batch size one agrees with exact delivery; batch size ten never exceeds its declared instruction delay in eligible execution.

## Verification and deliverables

Use known-deadline instruction streams, branch loops and masked-interrupt cases with reference traces.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Batch parameter finite and recorded; no timing-accuracy label for relaxed delivery.

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
