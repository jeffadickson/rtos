# TASK-NXR-184: Atomic ordering witnesses

Status: Planned

Requirement: NXR-184; qualification review 24/100.

## Required behavior

Atomic memory ordering claims shall be limited to the declared single-hart interrupt and device-ordering model.

## Reason and source finding

A single-hart stress pass cannot establish multicore memory-order semantics.

## Acceptance criteria

Release-acquire publication under preemption succeeds; unsupported multicore ordering is reported unsupported.

## Verification and deliverables

Run interrupt handoff litmus tests and inspect emitted fences with the selected compiler.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

No SMP qualification claim; bounded shared ordinary-RAM words.

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
