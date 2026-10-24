# TASK-NXR-200: Detection qualification campaign

Status: Planned

Requirement: NXR-200; qualification review 40/100.

## Required behavior

Each claimed invariant checker shall detect at least one controlled violation of its own property.

## Reason and source finding

Healthy runs alone cannot demonstrate that diagnostic checks discriminate failures.

## Acceptance criteria

Disabled or always-successful checkers fail qualification; every injected violation has an expected classification.

## Verification and deliverables

Create explicit controls for context corruption, duplicate jobs, stale handles and bad UART content.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Fault injection confined to named test profiles; no release default activation.

## Dependencies

NXR-072, NXR-073, NXR-094

## Inspected sources

- include/rtos_trace.hpp
- include/rtos_fault_record.hpp
- docs/hardware-in-loop.rst

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
