# TASK-NXR-212: Independent session supervision

Status: Planned

Requirement: NXR-212; qualification review 52/100.

## Required behavior

A blocked target shall not block progress collection, deadlines or cleanup for other targets.

## Reason and source finding

A shared blocking serial loop would turn one board failure into a bench-wide loss of evidence.

## Acceptance criteria

Stall one reader and disconnect another while a third completes its declared run without missed deadlines.

## Verification and deliverables

Execute a three-endpoint scenario with bounded asynchronous cancellation and retained per-board logs.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Finite per-board queues and deadlines; no unbounded task creation.

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
