# TASK-NXR-220: Host codec performance budget

Status: Planned

Requirement: NXR-220; qualification review 60/100.

## Required behavior

The host shall measure throughput and allocation behavior under the declared maximum board count without starving supervision.

## Reason and source finding

Async code alone does not prove bounded resource use at bench scale.

## Acceptance criteria

At configured board count and report rate all deadlines remain serviced; overload reports queue pressure explicitly.

## Verification and deliverables

Replay maximal frames from simulated endpoints and sample memory, CPU and per-board deadline delay.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Maximum board count, queue depth and host memory ceiling fixed before qualification.

## Dependencies

NXR-076, NXR-094

## Inspected sources

- docs/hil-verification-plan.rst
- requirements/hil-design.csv

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
