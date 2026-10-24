# TASK-NXR-226: Monitor noninterference budget

Status: Planned

Requirement: NXR-226; qualification review 66/100.

## Required behavior

The monitor shall publish its priority, sampling cost and transport budget so test results account for observer load.

## Reason and source finding

The test executive can perturb the scheduler behavior it is measuring.

## Acceptance criteria

Compare workload progress with telemetry disabled and enabled; overhead stays within the predeclared envelope.

## Verification and deliverables

Measure target cycles and external GPIO timing under summary and trace modes.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Default summary rate one per second; fixed service budget per dispatch.

## Dependencies

NXR-039, NXR-048

## Inspected sources

- src/kernel.cpp
- docs/hil-verification-plan.rst
- include/rtos_queue.hpp

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
