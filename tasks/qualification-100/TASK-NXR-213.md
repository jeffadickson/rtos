# TASK-NXR-213: Immutable start manifest

Status: Planned

Requirement: NXR-213; qualification review 53/100.

## Required behavior

Before START the host shall freeze requested and effective firmware, board, checker, schema and workload identities.

## Reason and source finding

Desired settings are not evidence that the target actually accepted those settings.

## Acceptance criteria

Firmware hash substitution or mismatched capability rejects START; later configuration edits create a new run identity.

## Verification and deliverables

Tamper with each manifest component between configuration and arming.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

One immutable manifest per run; checked file sizes and hashes.

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
