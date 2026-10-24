# TASK-NXR-198: Mismatch first witness

Status: Planned

Requirement: NXR-198; qualification review 38/100.

## Required behavior

A result mismatch shall retain the first worker, job sequence, operation and expected versus observed value before summarizing signatures.

## Reason and source finding

Aggregate mismatch counts do not identify the operation needed to reproduce a defect.

## Acceptance criteria

Two mismatches preserve the earliest witness; report truncation cannot erase its identity.

## Verification and deliverables

Inject a known bad job followed by a second fault and saturate normal telemetry.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

One fixed witness record plus bounded context window.

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
