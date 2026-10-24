# TASK-NXR-192: Schedule choice recording

Status: Planned

Requirement: NXR-192; qualification review 32/100.

## Required behavior

Replay reports shall distinguish deterministic workload inputs from nondeterministic interrupt and scheduling choices.

## Reason and source finding

Seeded jobs are reproducible even when their legal thread interleavings are not.

## Acceptance criteria

Identical seeds with different timer delivery produce separate schedule identities; exact replay is claimed only with sufficient recorded choices.

## Verification and deliverables

Repeat workloads with shifted interrupt points and compare input and schedule logs separately.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Fixed trace ring with explicit loss counters; loss disables exact replay claims.

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
