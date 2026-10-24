# TASK-NXR-229: Monitor parameter validation

Status: Planned

Requirement: NXR-229; qualification review 69/100.

## Required behavior

CONFIGURE shall validate worker selection, job bounds, durations and resource needs before changing the active configuration.

## Reason and source finding

Malformed test parameters can invalidate the resource guarantees of an otherwise bounded workload.

## Acceptance criteria

Overflowing durations, zero illegal counts and excessive frame demand fail without partial resource acquisition.

## Verification and deliverables

Test each parameter boundary and a compound invalid configuration against state snapshots.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Checked arithmetic and declared fixed capacities; no allocation on configure.

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
