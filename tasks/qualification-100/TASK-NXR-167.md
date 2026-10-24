# TASK-NXR-167: Capacity sweep qualification

Status: Planned

Requirement: NXR-167; qualification review 7/100.

## Required behavior

The qualification gate shall exercise legal minimum, representative and maximum resource capacities.

## Reason and source finding

Testing one default capacity misses index and fairness boundary defects.

## Acceptance criteria

Thread, queue and waiter boundaries reject excess operations without mutation at every selected capacity.

## Verification and deliverables

Run independent reference models and target boundary fixtures; retain configuration identity per case.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

No capacity-dependent allocation; total execution budget declared for each sweep.

## Dependencies

NXR-081, NXR-025

## Inspected sources

- include/rtos.hpp
- src/kernel.cpp
- src/scheduler.hpp
- docs/memory.rst

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
